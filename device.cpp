#include <climits>
#include <stdexcept>

#include "variable.h"
#include "display.h"
#include "device.h"

/* ************************************************************************** */

#if defined(ENABLE_SHT40)
	inline static float correct_SHT_temperature(float measured_value) {
		float slope = 1.000;
		float intercept = 0.000;
		float corrected_value = slope * measured_value + intercept;
		return corrected_value;
	}
#endif

/* ************************************************************************** */

#define MINIMUM_CPU_FREQUENCY 20

#if defined(ENABLE_OLED_OUTPUT)
	#undef MINIMUM_CPU_FREQUENCY
	#define MINIMUM_CPU_FREQUENCY 24
#endif

#if defined(ENABLE_GATEWAY)
	#undef MINIMUM_CPU_FREQUENCY
	#define MINIMUM_CPU_FREQUENCY 80
#endif

unsigned long int const CPU_frequency =
	#if defined(CPU_FREQUENCY)
		CPU_FREQUENCY < MINIMUM_CPU_FREQUENCY
			? MINIMUM_CPU_FREQUENCY
			: CPU_FREQUENCY
	#else
		0
	#endif
	;

std::mutex device_mutex;

inline static void OLED_space_or_newline(void) {
	#if defined(OLED_HORIZONAL)
		OLED::print(' ');
	#else
		OLED::println();
	#endif
}

/* ************************************************************************** */

namespace Setting {
	unsigned int active_sensors[num_of_sensors] = DEFAULT_SENSOR_SETTING;
	char const *const *const upload_names[] = HTTP_UPLOAD_FIELDS;

	static bool FIELD_INT_read(void *const memory, char const *const string) {
		char *remaining = nullptr;
		int const result = strtol(string, &remaining, 10);
		if (remaining == nullptr || !*remaining) return false;
		*static_cast<int *>(memory) = result;
		return true;
	}
	static class String FIELD_INT_write(void const *const memory) {
		return String(*static_cast<int const *>(memory));
	}
	static constexpr struct FieldAccess const FIELD_INT = {sizeof (float), FIELD_INT_read, FIELD_INT_write};

	static bool FIELD_FLOAT_read(void *const memory, char const *const string) {
		char *remaining = nullptr;
		float const result = strtof(string, &remaining);
		if (remaining == nullptr || !*remaining) return false;
		*static_cast<float *>(memory) = result;
		return true;
	}
	static class String FIELD_FLOAT_write(void const *const memory) {
		return String(*static_cast<float const *>(memory));
	}
	static constexpr struct FieldAccess const FIELD_FLOAT = {sizeof (float), FIELD_FLOAT_read, FIELD_FLOAT_write};

	constinit struct SensorField const *const device_fields[] = {
		[0] = (struct SensorField const []){
			{{sizeof (struct FullTime)}},
			{{0}}
		},
		[battery] = (struct SensorField const []){
			{FIELD_FLOAT, "Battery",         "Power",  "V"},
			{FIELD_FLOAT, "Battery",         "Power",  "%"},
			{{0}}
		},
		[Dallas] = (struct SensorField const []){
			{FIELD_FLOAT, "Dallas temp.",    "Dallas", "deg C"},
			{{0}}
		},
		[SHT40] = (struct SensorField const []){
			{FIELD_FLOAT, "SHT40 temp.",     "SHT40",  "deg C"},
			{FIELD_FLOAT, "SHT40 humidity",  "SHT40",  "%RH"},
			{{0}}
		},
		[BME280] = (struct SensorField const []){
			{FIELD_FLOAT, "BME280 temp.",    "BME280", "deg C"},
			{FIELD_FLOAT, "BME280 pressure", "BME280", "Pa"},
			{FIELD_FLOAT, "BME280 humidity", "BME280", "%RH"},
			{{0}}
		},
		[LTR390] = (struct SensorField const []){
			{FIELD_FLOAT, "LTR390 UV",       "LTR UV", ""},
			{{0}}
		}
	};

	std::map<unsigned int, unsigned int> save(void) {
		std::map<unsigned int, unsigned int> map;
		for (unsigned int i = 1; i < num_of_sensors; ++i) {
			unsigned int const choice = active_sensors[i];
			if (active_sensors[i])
				map[i] = active_sensors[i];
		}
		return map;
	}

	void load(std::map<unsigned int, unsigned int> const *const map) {
		active_sensors[0] = UINT_MAX;
		for (unsigned int i = 1; i < num_of_sensors; ++i) {
			std::map<unsigned int, unsigned int>::const_iterator found = map->find(i);
			if (found != map->end())
				active_sensors[i] = found->second;
		}
	}
}

/* ************************************************************************** */

#if !defined(ENABLE_CLOCK)
	#include <RTClib.h>

	namespace RTC {
		static bool clock_available;
		static class RTC_Millis internal_clock;

		bool initialize(void) {
			clock_available = false;
			return true;
		}

		void set(struct FullTime const *const fulltime) {
			class DateTime const datetime(
				fulltime->year, fulltime->month, fulltime->day,
				fulltime->hour, fulltime->minute, fulltime->second
			);
			if (clock_available) {
				internal_clock.adjust(datetime);
			}
			else {
				internal_clock.begin(datetime);
				clock_available = true;
			}
		}

		bool now(struct FullTime *const fulltime) {
			class DateTime const datetime = internal_clock.now();
			if (fulltime != NULL)
				*fulltime = {
					.year = (unsigned short int)datetime.year(),
					.month = (unsigned char)datetime.month(),
					.day = (unsigned char)datetime.day(),
					.hour = (unsigned char)datetime.hour(),
					.minute = (unsigned char)datetime.minute(),
					.second = (unsigned char)datetime.second()
				};
			return clock_available;
		}
	}
#elif ENABLE_CLOCK == CLOCK_PCF85063TP
	#include <PCF85063TP.h>

	namespace RTC {
		static class PCD85063TP external_clock;

		bool initialize(void) {
			external_clock.begin();
			external_clock.startClock();
			return true;
		}

		void set(struct FullTime const *const fulltime) {
			external_clock.stopClock();
			external_clock.fillByYMD(fulltime->year, fulltime->month, fulltime->day);
			external_clock.fillByHMS(fulltime->hour, fulltime->minute, fulltime->second);
			external_clock.setTime();
			external_clock.startClock();
		}

		bool now(struct FullTime *const fulltime) {
			external_clock.getTime();
			if (fulltime != NULL)
				*fulltime = {
					.year = (unsigned short int)(2000U + external_clock.year),
					.month = external_clock.month,
					.day = external_clock.dayOfMonth,
					.hour = external_clock.hour,
					.minute = external_clock.minute,
					.second = external_clock.second
				};
			static bool available = false;
			if (!available)
				available =
					1 <= external_clock.year       && external_clock.year       <= 99 &&
					1 <= external_clock.month      && external_clock.month      <= 12 &&
					1 <= external_clock.dayOfMonth && external_clock.dayOfMonth <= 30 &&
					0 <= external_clock.hour       && external_clock.hour       <= 23 &&
					0 <= external_clock.minute     && external_clock.minute     <= 59 &&
					0 <= external_clock.second     && external_clock.second     <= 59;
			return available;
		}
	}
#elif ENABLE_CLOCK == CLOCK_DS1307 || ENABLE_CLOCK == CLOCK_DS3231
	#include <RTClib.h>

	namespace RTC {
		#if ENABLE_CLOCK == CLOCK_DS1307
			static class RTC_DS1307 external_clock;
		#elif ENABLE_CLOCK == CLOCK_DS3231
			static class RTC_DS3231 external_clock;
		#endif

		bool initialize(void) {
			if (!external_clock.begin()) {
				OLED_LOCK(oled_lock);
				Display::println("Clock not found");
				return false;
			}
			#if ENABLE_CLOCK == CLOCK_DS1307
				if (!external_clock.isrunning()) {
					DEVICE_LOCK(device_lock);
					Display::println("DS1307 not running");
					return false;
				}
			#endif
			return true;
		}

		void set(struct FullTime const *const fulltime) {
			class DateTime const datetime(
				fulltime->year, fulltime->month, fulltime->day,
				fulltime->hour, fulltime->minute, fulltime->second
			);
			external_clock.adjust(datetime);
		}

		bool now(struct FullTime *const fulltime) {
			class DateTime const datetime = external_clock.now();
			if (fulltime != NULL)
				*fulltime = {
					.year = datetime.year(),
					.month = datetime.month(),
					.day = datetime.day(),
					.hour = datetime.hour(),
					.minute = datetime.minute(),
					.second = datetime.second()
				};
			return datetime.isValid();
		}
	}
#endif

namespace NTP {
	static WiFiUDP WiFiUDP;
	static class NTPClient NTPClient(WiFiUDP, NTP_SERVER, 0, NTP_INTERVAL);

	void initialize(void) {
		NTPClient.begin();
	}

	bool now(struct FullTime *const fulltime) {
		if (!NTPClient.isTimeSet()) return false;
		time_t const epoch = NTPClient.getEpochTime();
		struct tm time;
		gmtime_r(&epoch, &time);
		*fulltime = {
			.year = (unsigned short int)(1900U + time.tm_year),
			.month = (unsigned char)(time.tm_mon + 1),
			.day = (unsigned char)time.tm_mday,
			.hour = (unsigned char)time.tm_hour,
			.minute = (unsigned char)time.tm_min,
			.second = (unsigned char)time.tm_sec
		};
		return true;
	}

	void synchronize(void) {
		if (NTPClient.update()) {
			time_t const epoch = NTPClient.getEpochTime();
			struct tm time;
			gmtime_r(&epoch, &time);
			struct FullTime const fulltime = {
				.year = (unsigned short int)(1900U + time.tm_year),
				.month = (unsigned char)(time.tm_mon + 1),
				.day = (unsigned char)time.tm_mday,
				.hour = (unsigned char)time.tm_hour,
				.minute = (unsigned char)time.tm_min,
				.second = (unsigned char)time.tm_sec
			};
			RTC::set(&fulltime);
			COM::println("NTP update");
		}
	}
}

#if defined(ENABLE_BATTERY_GAUGE)
	#if defined(ENABLE_BATTERY_GAUGE_LC709203F)
		#include <Adafruit_LC709203F.h>

		static class Adafruit_LC709203F lc709203f;
	#endif
	#if defined(ENABLE_BATTERY_GAUGE_MAX17043)
		#include <DFRobot_MAX17043.h>

		static class DFRobot_MAX17043 max17043;
	#endif
#endif

#ifdef ENABLE_DALLAS
	#include <OneWire.h>
	#include <DallasTemperature.h>

	static class OneWire onewire_thermometer(ENABLE_DALLAS);
	static class DallasTemperature dallas(&onewire_thermometer);
#endif

#if defined(ENABLE_SHT40) || defined(ENABLE_BME280)
	#include <Adafruit_Sensor.h>
#endif

#ifdef ENABLE_SHT40
	#include <Adafruit_SHT4x.h>

	static class Adafruit_SHT4x SHT = Adafruit_SHT4x();
#endif

#ifdef ENABLE_BME280
	#include <Adafruit_BME280.h>

	static class Adafruit_BME280 BME;
#endif

#ifdef ENABLE_LTR390
	#include <Adafruit_LTR390.h>
	static class Adafruit_LTR390 LTR;
#endif

/* ************************************************************************** */

size_t NewData::total_size;
size_t NewData::offset[Setting::num_of_sensors];

void NewData::initialize(void) {
	total_size = 0;
	for (unsigned int i = 0; i < Setting::num_of_sensors; ++i) {
		size_t device_size = 0;
		unsigned int j = 0;
		for (;;) {
			size_t const field_size = Setting::device_fields[i][j].access.size;
			if (!field_size) break;
			device_size += field_size;
			++j;
		}
		offset[i] = total_size;
		if (Setting::active_sensors[i]) total_size += device_size;
	}
}

void NewData::writeln(class Print *const print) const {
	{
		struct FullTime const *const time = this->get_time();
		print->printf(
			"%04u-%02u-%02uT%02u:%02u:%02uZ,",
			time->year, time->month, time->day,
			time->hour, time->minute, time->second
		);
	}

	#if defined(ENABLE_BATTERY_GAUGE)
		if (Setting::active_sensors[Setting::battery]) {
			float const *const values = this->device_pointer<float>(Setting::battery);
			print->printf("%f,%f,", values[0], values[1]);
		}
	#endif

	#if defined(ENABLE_DALLAS)
		if (Setting::active_sensors[Setting::Dallas])
			print->printf("%f,", *this->device_pointer<float>(Setting::Dallas));
	#endif

	#if defined(ENABLE_SHT40)
		if (Setting::active_sensors[Setting::SHT40]) {
			float const *const values = this->device_pointer<float>(Setting::SHT40);
			print->printf("%f,%f,", values[0], values[1]);
		}
	#endif

	#if defined(ENABLE_BME280)
		if (Setting::active_sensors[Setting::BME280]) {
			float const *const values = this->device_pointer<float>(Setting::BME280);
			print->printf("%f,%f,%f,", values[0], values[1], values[2]);
		}
	#endif

	#if defined(ENABLE_LTR390)
		if (Setting::active_sensors[Setting::LTR390])
			print->printf("%f,", *this->device_pointer<float>(Setting::LTR390));
	#endif

	print->write('\n');
}

bool NewData::readln(class Stream *const stream) {
	{
		class String const s = stream->readStringUntil(',');
		struct FullTime *const time = this->get_time();
		if (
			sscanf(
				s.c_str(),
				"%4hu-%2hhu-%2hhuT%2hhu:%2hhu:%2hhuZ",
				&time->year, &time->month, &time->day,
				&time->hour, &time->minute, &time->second
			) != 6
		) return false;
	}

	#ifdef ENABLE_BATTERY_GAUGE
		if (Setting::active_sensors[Setting::battery]) {
			float *const values = this->device_pointer<float>(Setting::battery);
			class String const voltage = stream->readStringUntil(',');
			if (sscanf(voltage.c_str(), "%f", values+0) != 1) return false;
			class String const percentage = stream->readStringUntil(',');
			if (sscanf(percentage.c_str(), "%f", values+1) != 1) return false;
		}
	#endif

	#ifdef ENABLE_DALLAS
		if (Setting::active_sensors[Setting::Dallas]) {
			float *const value = this->device_pointer<float>(Setting::Dallas);
			class String const temperature = stream->readStringUntil(',');
			if (sscanf(temperature.c_str(), "%f", value) != 1) return false;
		}
	#endif

	#ifdef ENABLE_SHT40
		if (Setting::active_sensors[Setting::SHT40]) {
			float *const values = this->device_pointer<float>(Setting::SHT40);
			class String const temperature = stream->readStringUntil(',');
			if (sscanf(temperature.c_str(), "%f", values+0) != 1) return false;
			class String const humidity = stream->readStringUntil(',');
			if (sscanf(humidity.c_str(), "%f", values+1) != 1) return false;
		}
	#endif

	#ifdef ENABLE_BME280
		if (Setting::active_sensors[Setting::BME280]) {
			float *const values = this->device_pointer<float>(Setting::BME280);
			class String const temperature = stream->readStringUntil(',');
			if (sscanf(temperature.c_str(), "%f", values+0) != 1) return false;
			class String const pressure = stream->readStringUntil(',');
			if (sscanf(pressure.c_str(), "%f", values+1) != 1) return false;
			class String const humidity = stream->readStringUntil(',');
			if (sscanf(humidity.c_str(), "%f", values+2) != 1) return false;
		}
	#endif

	#ifdef ENABLE_LTR390
		if (Setting::active_sensors[Setting::LTR390]) {
			float *const value = this->device_pointer<float>(Setting::LTR390);
			class String const ultraviolet = stream->readStringUntil(',');
			if (sscanf(ultraviolet.c_str(), "%f", value) != 1) return false;
		}
	#endif

	stream->readStringUntil('\n');
	return true;
}

void NewData::println(void) const {
	COM::print("Time: ");
	Display::println(String(*this->get_time()));

	#if defined(ENABLE_BATTERY_GAUGE)
		if (Setting::active_sensors[Setting::battery]) {
			float const *const values = this->device_pointer<float>(Setting::battery);
			Display::print("Battery: ");
			Display::print(values[0], 1);
			Display::print("V ");
			Display::print(values[1], 0);
			Display::println("%");
		}
	#endif

	#if defined(ENABLE_DALLAS)
		if (Setting::active_sensors[Setting::Dallas]) {
			Display::print("Dallas temp.: ");
			Display::println(*this->device_pointer<float>(Setting::Dallas));
		}
	#endif

	#if defined(ENABLE_SHT40)
		if (Setting::active_sensors[Setting::SHT40]) {
			float const *const values = this->device_pointer<float>(Setting::SHT40);
			Display::print("SHT temp.: ");
			Display::println(values[0], 1);
			Display::print("SHT humidity: ");
			Display::println(values[1], 0);
		}
	#endif

	#if defined(ENABLE_BME280)
		if (Setting::active_sensors[Setting::BME280]) {
			float const *const values = this->device_pointer<float>(Setting::BME280);
			Display::print("BME temp.: ");
			Display::println(values[0], 1);
			Display::print("BME pressure: ");
			Display::println(values[1], 0);
			Display::print("BME humidity: ");
			Display::println(values[2], 0);
		}
	#endif

	#if defined(ENABLE_LTR390)
		if (Setting::active_sensors[Setting::LTR390]) {
			float const *const value = this->device_pointer<float>(Setting::LTR390);
			Display::print("LTR UV: ");
			Display::println(*value);
		}
	#endif
}

void NewData::dashboard(void) const {
	OLED::println(static_cast<unsigned int>(Variable::device_id));
	struct FullTime time = *this->get_time();
	#if defined(DASHBOARD_TIMEZONE)
		time += DASHBOARD_TIMEZONE;
	#endif
	#if defined(OLED_HORIZONAL)
		OLED::print(static_cast<unsigned int>(time.day));
		OLED::print('/');
		OLED::print(static_cast<unsigned int>(time.month));
		OLED::print('/');
		OLED::println(time.year);
	#else
		OLED::println(time.year);
		OLED::print(static_cast<unsigned int>(time.day));
		OLED::print('/');
		OLED::println(static_cast<unsigned int>(time.month));
	#endif
	if (time.hour < 10) OLED::print('0');
	OLED::print(static_cast<unsigned int>(time.hour));
	OLED::print(':');
	if (time.minute < 10) OLED::print('0');
	OLED::println(static_cast<unsigned int>(time.minute));
	#if defined(OLED_HORIZONAL)
		OLED::println();
	#endif

	static unsigned int state = 0;
	do {
		if (state < __LINE__) {
			state = __LINE__;
			OLED::println("Every");
			OLED::print(Variable::measure_interval / 60000., 2);
			OLED_space_or_newline();
			OLED::println("min.");
		}
	#if defined(ENABLE_BATTERY_GAUGE)
		else if (state < __LINE__ && Setting::active_sensors[Setting::battery]) {
			state = __LINE__;
			OLED::println("Power");
			OLED::print(this->device_pointer<float>(Setting::battery)[0], 1);
			OLED_space_or_newline();
			OLED::print("V");
		}
		else if (state < __LINE__ && Setting::active_sensors[Setting::battery]) {
			state = __LINE__;
			OLED::println("Power");
			OLED::print(this->device_pointer<float>(Setting::battery)[1], 0);
			OLED_space_or_newline();
			OLED::print("%");
		}
	#endif
	#if defined(ENABLE_DALLAS)
		else if (state < __LINE__ && Setting::active_sensors[Setting::Dallas]) {
			state = __LINE__;
			OLED::println("Dallas");
			OLED::print(this->device_pointer<float>(Setting::Dallas)[0]);
			OLED_space_or_newline();
			OLED::print("deg C");
		}
	#endif
	#if defined(ENABLE_SHT40)
		else if (state < __LINE__ && Setting::active_sensors[Setting::SHT40]) {
			state = __LINE__;
			OLED::println("SHT40");
			OLED::print(this->device_pointer<float>(Setting::SHT40)[0]);
			OLED_space_or_newline();
			OLED::print("deg C");
		}
		else if (state < __LINE__ && Setting::active_sensors[Setting::SHT40]) {
			state = __LINE__;
			OLED::println("SHT40");
			OLED::print(this->device_pointer<float>(Setting::SHT40)[1]);
			OLED_space_or_newline();
			OLED::print("%RH");
		}
	#endif
	#if defined(ENABLE_BME280)
		else if (state < __LINE__ && Setting::active_sensors[Setting::BME280]) {
			state = __LINE__;
			OLED::println("BME280");
			OLED::print(this->device_pointer<float>(Setting::BME280)[0], 1);
			OLED_space_or_newline();
			OLED::print("deg C");
		}
		else if (state < __LINE__ && Setting::active_sensors[Setting::BME280]) {
			state = __LINE__;
			OLED::println("BME280");
			OLED::print(this->device_pointer<float>(Setting::BME280)[1], 0);
			OLED_space_or_newline();
			OLED::print("Pa");
		}
		else if (state < __LINE__ && Setting::active_sensors[Setting::BME280]) {
			state = __LINE__;
			OLED::println("BME280");
			OLED::print(this->device_pointer<float>(Setting::BME280)[2], 0);
			OLED_space_or_newline();
			OLED::print("%RH");
		}
	#endif
	#if defined(ENABLE_LTR390)
		else if (state < __LINE__ && Setting::active_sensors[Setting::LTR390]) {
			state = __LINE__;
			OLED::println("LTR");
			OLED::print(this->device_pointer<float>(Setting::LTR390)[0]);
			OLED_space_or_newline();
			OLED::print("UV");
		}
	#endif
		else
			state = 0;
	}
	while (!state);
}

/* ************************************************************************** */

void Data::writeln(class Print *const print) const {
	print->printf(
		"%04u-%02u-%02uT%02u:%02u:%02uZ,",
		this->time.year, this->time.month, this->time.day,
		this->time.hour, this->time.minute, this->time.second
	);

	#ifdef ENABLE_BATTERY_GAUGE
		print->printf(
			"%f,%f,",
			this->battery_voltage, this->battery_percentage
		);
	#endif

	#ifdef ENABLE_DALLAS
		print->printf(
			"%f,",
			this->dallas_temperature
		);
	#endif

	#ifdef ENABLE_SHT40
		print->printf(
			"%f,%f,",
			this->sht40_temperature, this->sht40_humidity
		);
	#endif

	#ifdef ENABLE_BME280
		print->printf(
			"%f,%f,%f,",
			this->bme280_temperature, this->bme280_pressure, this->bme280_humidity
		);
	#endif

	#ifdef ENABLE_LTR390
		print->printf(
			"%f,",
			this->ltr390_ultraviolet
		);
	#endif

	print->write('\n');
}

bool Data::readln(class Stream *const stream) {
	/* Time */
	{
		class String const s = stream->readStringUntil(',');
		if (
			sscanf(
				s.c_str(),
				"%4hu-%2hhu-%2hhuT%2hhu:%2hhu:%2hhuZ",
				&this->time.year, &this->time.month, &this->time.day,
				&this->time.hour, &this->time.minute, &this->time.second
			) != 6
		) return false;
	}

	/* Battery gauge */
	#ifdef ENABLE_BATTERY_GAUGE
		{
			class String const s = stream->readStringUntil(',');
			if (sscanf(s.c_str(), "%f", &this->battery_voltage) != 1) return false;
		}
		{
			class String const s = stream->readStringUntil(',');
			if (sscanf(s.c_str(), "%f", &this->battery_percentage) != 1) return false;
		}
	#endif

	/* Dallas thermometer */
	#ifdef ENABLE_DALLAS
		{
			class String const s = stream->readStringUntil(',');
			if (sscanf(s.c_str(), "%f", &this->dallas_temperature) != 1) return false;
		}
	#endif

	/* SHT40 sensor */
	#ifdef ENABLE_SHT40
		{
			class String const s = stream->readStringUntil(',');
			if (sscanf(s.c_str(), "%f", &this->sht40_temperature) != 1) return false;
		}
		{
			class String const s = stream->readStringUntil(',');
			if (sscanf(s.c_str(), "%f", &this->sht40_humidity) != 1) return false;
		}
	#endif

	/* BME280 sensor */
	#ifdef ENABLE_BME280
		{
			class String const s = stream->readStringUntil(',');
			if (sscanf(s.c_str(), "%f", &this->bme280_temperature) != 1) return false;
		}
		{
			class String const s = stream->readStringUntil(',');
			if (sscanf(s.c_str(), "%f", &this->bme280_pressure) != 1) return false;
		}
		{
			class String const s = stream->readStringUntil(',');
			if (sscanf(s.c_str(), "%f", &this->bme280_humidity) != 1) return false;
		}
	#endif

	/* LTR390 sensor */
	#ifdef ENABLE_LTR390
		{
			class String const s = stream->readStringUntil('\n');
			if (sscanf(s.c_str(), "%f", &this->ltr390_ultraviolet) != 1) return false;
		}
	#endif

	stream->readStringUntil('\n');
	return true;
}

void Data::println(void) const {
	COM::print("Time: ");
	Display::println(String(this->time));

	#if defined(ENABLE_BATTERY_GAUGE)
		Display::print("Battery: ");
		Display::print(this->battery_voltage, 1);
		Display::print("V ");
		Display::print(this->battery_percentage, 0);
		Display::println("%");
	#endif

	#if defined(ENABLE_DALLAS)
		Display::print("Dallas temp.: ");
		Display::println(this->dallas_temperature);
	#endif

	#if defined(ENABLE_SHT40)
		Display::print("SHT temp.: ");
		Display::println(this->sht40_temperature, 1);
		Display::print("SHT humidity: ");
		Display::println(this->sht40_humidity, 0);
	#endif

	#if defined(ENABLE_BME280)
		Display::print("BME temp.: ");
		Display::println(this->bme280_temperature, 1);
		Display::print("BME pressure: ");
		Display::println(this->bme280_pressure, 0);
		Display::print("BME humidity: ");
		Display::println(this->bme280_humidity, 0);
	#endif

	#if defined(ENABLE_LTR390)
		Display::print("LTR UV: ");
		Display::println(this->ltr390_ultraviolet);
	#endif
}

void Data::dashboard(void) const {
	OLED::println(static_cast<unsigned int>(Variable::device_id));
	struct FullTime time = this->time;
	#if defined(DASHBOARD_TIMEZONE)
		time += DASHBOARD_TIMEZONE;
	#endif
	#if defined(OLED_HORIZONAL)
		OLED::print(static_cast<unsigned int>(time.day));
		OLED::print('/');
		OLED::print(static_cast<unsigned int>(time.month));
		OLED::print('/');
		OLED::println(time.year);
	#else
		OLED::println(time.year);
		OLED::print(static_cast<unsigned int>(time.day));
		OLED::print('/');
		OLED::println(static_cast<unsigned int>(time.month));
	#endif
	if (time.hour < 10) OLED::print('0');
	OLED::print(static_cast<unsigned int>(time.hour));
	OLED::print(':');
	if (time.minute < 10) OLED::print('0');
	OLED::println(static_cast<unsigned int>(time.minute));
	#if defined(OLED_HORIZONAL)
		OLED::println();
	#endif

	static unsigned int state = 0;
	do {
		if (state < __LINE__) {
			state = __LINE__;
			OLED::println("Measure");
			OLED::print(Variable::measure_interval / 60000);
			OLED_space_or_newline();
			OLED::println("minutes");
		}
	#if defined(ENABLE_BATTERY_GAUGE)
		else if (state < __LINE__) {
			state = __LINE__;
			OLED::println("Power");
			OLED::print(this->battery_voltage, 1);
			OLED_space_or_newline();
			OLED::print("V");
		}
		else if (state < __LINE__) {
			state = __LINE__;
			OLED::println("Power");
			OLED::print(this->battery_percentage, 0);
			OLED_space_or_newline();
			OLED::print("%");
		}
	#endif
	#if defined(ENABLE_DALLAS)
		else if (state < __LINE__) {
			state = __LINE__;
			OLED::println("Dallas");
			OLED::print(this->dallas_temperature);
			OLED_space_or_newline();
			OLED::print("deg C");
		}
	#endif
	#if defined(ENABLE_SHT40)
		else if (state < __LINE__) {
			state = __LINE__;
			OLED::println("SHT40");
			OLED::print(this->sht40_temperature);
			OLED_space_or_newline();
			OLED::print("deg C");
		}
		else if (state < __LINE__) {
			state = __LINE__;
			OLED::println("SHT40");
			OLED::print(this->sht40_humidity);
			OLED_space_or_newline();
			OLED::print("%RH");
		}
	#endif
	#if defined(ENABLE_BME280)
		else if (state < __LINE__) {
			state = __LINE__;
			OLED::println("BME280");
			OLED::print(this->bme280_temperature, 1);
			OLED_space_or_newline();
			OLED::print("deg C");
		}
		else if (state < __LINE__) {
			state = __LINE__;
			OLED::println("BME280");
			OLED::print(this->bme280_pressure, 0);
			OLED_space_or_newline();
			OLED::print("Pa");
		}
		else if (state < __LINE__) {
			state = __LINE__;
			OLED::println("BME280");
			OLED::print(this->bme280_humidity, 0);
			OLED_space_or_newline();
			OLED::print("%RH");
		}
	#endif
	#if defined(ENABLE_LTR390)
		else if (state < __LINE__) {
			state = __LINE__;
			OLED::println("LTR");
			OLED::print(this->ltr390_ultraviolet);
			OLED_space_or_newline();
			OLED::print("UV");
		}
	#endif
		else
			state = 0;
	}
	while (!state);
}

namespace Sensor {
	bool initialize(void) {
		if (!RTC::initialize()) return false;
		if (!Variable::enable_measure) return true;
		DEVICE_LOCK(device_lock);

		/* Initial battery gauge */
		#if defined(ENABLE_BATTERY_GAUGE_LC709203F)
			if (Setting::active_sensors[Setting::battery] == Setting::LC709203F)
				lc709203f.begin();
		#endif
		#if defined(ENABLE_BATTERY_GAUGE_MAX17043)
			if (Setting::active_sensors[Setting::battery] == Setting::MAX17043)
				max17043.begin();
		#endif

		/* Initialize Dallas thermometer */
		#if defined(ENABLE_DALLAS)
			if (Setting::active_sensors[Setting::Dallas]) {
				dallas.begin();
				DeviceAddress thermometer_address;
				if (dallas.getAddress(thermometer_address, 0)) {
					Display::println("Dallas thermometer found");
				}
				else {
					Display::println("Dallas thermometer not found");
					return false;
				}
			}
		#endif

		/* Initialize SHT40 sensor */
		#if defined(ENABLE_SHT40)
			if (Setting::active_sensors[Setting::SHT40]) {
				if (SHT.begin()) {
					Display::println("SHT40 sensor found");
				}
				else {
					Display::println("SHT40 sensor not found");
					return false;
				}
				SHT.setPrecision(SHT4X_HIGH_PRECISION);
				SHT.setHeater(SHT4X_NO_HEATER);
			}
		#endif

		/* Initialize BME280 sensor */
		#if defined(ENABLE_BME280)
			if (Setting::active_sensors[Setting::BME280]) {
				if (BME.begin()) {
					Display::println("BME280 sensor found");
				}
				else {
					Display::println("BME280 sensor not found");
					return false;
				}
			}
		#endif

		/* Initial LTR390 sensor */
		#if defined(ENABLE_LTR390)
			if (Setting::active_sensors[Setting::LTR390]) {
				if (LTR.begin()) {
					LTR.setMode(LTR390_MODE_UVS);
					Display::println("LTR390 sensor found");
				}
				else {
					Display::println("LTR390 sensor not found");
					return false;
				}
			}
		#endif

		return true;
	}

	bool measure(union NewData *const data) {
		DEVICE_LOCK(device_lock);
		if (!RTC::now(data->get_time()))
			return false;

		#if defined(ENABLE_BATTERY_GAUGE)
			{
				enum Setting::battery const type = static_cast<enum Setting::battery>(Setting::active_sensors[Setting::battery]);
				if (!type) ;
				#if defined(ENABLE_BATTERY_GAUGE_LC709203F)
					else if (type == Setting::LC709203F) {
						float *const values = data->device_pointer<float>(Setting::battery);
						values[0] = lc709203f.cellVoltage();
						values[1] = lc709203f.cellPercent();
					}
				#endif
				#if defined(ENABLE_BATTERY_GAUGE_MAX17043)
					else if (type == Setting::MAX17043) {
						float *const values = data->device_pointer<float>(Setting::battery);
						values[0] = max17043.readVoltage() / 1000;
						values[1] = max17043.readPercentage();
					}
				#endif
			}
		#endif

		#if defined(ENABLE_DALLAS)
			if (Setting::active_sensors[Setting::Dallas]) {
				float *const value = data->device_pointer<float>(Setting::Dallas);
				dallas.requestTemperatures();
				delay(750);
				*value = dallas.getTempCByIndex(0);
			}
		#endif

		#if defined(ENABLE_SHT40)
			if (Setting::active_sensors[Setting::SHT40]) {
				float *const values = data->device_pointer<float>(Setting::SHT40);
				sensors_event_t temperature_event, humidity_event;
				SHT.getEvent(&humidity_event, &temperature_event);
				values[0] = correct_SHT_temperature(temperature_event.temperature);
				values[1] = humidity_event.relative_humidity;
			}
		#endif

		#if defined(ENABLE_BME280)
			if (Setting::active_sensors[Setting::BME280]) {
				float *const values = data->device_pointer<float>(Setting::BME280);
				values[0] = BME.readTemperature();
				values[1] = BME.readPressure();
				values[2] = BME.readHumidity();
			}
		#endif

		#if defined(ENABLE_LTR390)
			if (Setting::active_sensors[Setting::LTR390]) {
				float *const value = data->device_pointer<float>(Setting::Dallas);
				*value = LTR.readUVS();
			}
		#endif

		return true;
	}
}

/* ************************************************************************** */
