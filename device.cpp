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
/* Include headers of libraries and define related global variables */

#if defined(ENABLE_BATTERY_GAUGE_LC709203F)
	#include <Adafruit_LC709203F.h>

	static class Adafruit_LC709203F lc709203f;
#endif
#if defined(ENABLE_BATTERY_GAUGE_MAX17043)
	#include <DFRobot_MAX17043.h>

	static class DFRobot_MAX17043 max17043;
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
	class Maybe<unsigned int> active_sensors[num_of_sensors] = DEFAULT_SENSOR_SETTING;

	static bool FIELD_INT_read(void *const memory, char const *const string) {
		char *remaining = nullptr;
		int const result = strtol(string, &remaining, 10);
		if (remaining == nullptr || *remaining) return false;
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
		if (remaining == nullptr || *remaining) return false;
		*static_cast<float *>(memory) = result;
		return true;
	}
	static class String FIELD_FLOAT_write(void const *const memory) {
		return String(*static_cast<float const *>(memory));
	}
	static constexpr struct FieldAccess const FIELD_FLOAT = {sizeof (float), FIELD_FLOAT_read, FIELD_FLOAT_write};

	constinit struct SensorField const *const sensor_fields[] = {
		[0] = (struct SensorField const []){
			{{sizeof (struct FullTime)}},
			{{0}}
		},
		[battery] = (struct SensorField const []){
			{FIELD_FLOAT, "Power",  "V"},
			{FIELD_FLOAT, "Power",  "%"},
			{{0}}
		},
		[Dallas] = (struct SensorField const []){
			{FIELD_FLOAT, "Dallas", "degC"},
			{{0}}
		},
		[SHT40] = (struct SensorField const []){
			{FIELD_FLOAT, "SHT40",  "degC"},
			{FIELD_FLOAT, "SHT40",  "%RH"},
			{{0}}
		},
		[BME280] = (struct SensorField const []){
			{FIELD_FLOAT, "BME280", "degC"},
			{FIELD_FLOAT, "BME280", "Pa"},
			{FIELD_FLOAT, "BME280", "%RH"},
			{{0}}
		},
		[LTR390] = (struct SensorField const []){
			{FIELD_FLOAT, "LTR390", "UV"},
			{{0}}
		}
	};

	void save(std::map<unsigned int, unsigned int> *const map) {
		map->clear();
		for (unsigned int i = 1; i < num_of_sensors; ++i)
			if (active_sensors[i].isJust())
				(*map)[i] = active_sensors[i].unwrap();
	}

	void load(std::map<unsigned int, unsigned int> const *const map) {
		active_sensors[0] = Maybe(UINT_MAX);
		for (unsigned int i = 1; i < num_of_sensors; ++i) {
			std::map<unsigned int, unsigned int>::const_iterator found = map->find(i);
			if (found == map->end())
				active_sensors[i] = Maybe<unsigned int>();
			else
				active_sensors[i] = Maybe(found->second);
		}
	}
}

/* ************************************************************************** */

#if defined(ENABLE_CLOCK_PCF85063TP)
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
#elif defined(ENABLE_CLOCK_DS1307) || defined(ENABLE_CLOCK_DS3231)
	#include <RTClib.h>

	namespace RTC {
		#if defined(ENABLE_CLOCK_DS1307)
			static class RTC_DS1307 external_clock;
		#elif defined(ENABLE_CLOCK_DS3231)
			static class RTC_DS3231 external_clock;
		#endif

		bool initialize(void) {
			if (!external_clock.begin()) {
				OLED_LOCK(oled_lock);
				Display::println("Clock not found");
				return false;
			}
			#if defined(ENABLE_CLOCK_DS1307)
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
#else
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

/* ************************************************************************** */

size_t Data::total_size;
size_t Data::offset[Setting::num_of_sensors];

void Data::initialize(void) {
	total_size = 0;
	for (unsigned int sensor = 0; sensor < Setting::num_of_sensors; ++sensor) {
		size_t device_size = 0;
		size_t field = 0;
		for (;;) {
			size_t const field_size = Setting::sensor_fields[sensor][field].access.size;
			if (!field_size) break;
			device_size += field_size;
			++field;
		}
		offset[sensor] = total_size;
		if (Setting::active_sensors[sensor].isJust())
			total_size += device_size;
	}
}

void Data::writeln(class Print *const print) const {
	/* write measure time */
	struct FullTime const *const time = this->get_time();
	print->printf(
		"%04u-%02u-%02uT%02u:%02u:%02uZ,",
		time->year, time->month, time->day,
		time->hour, time->minute, time->second
	);

	/* write measured values across all fields of all active sensors */
	void const *p = pointer_offset<void const, union Data const>(this, offset[1]);
	for (unsigned int sensor = 1; sensor < Setting::num_of_sensors; ++sensor) {
		if (Setting::active_sensors[sensor].isJust()) {
			size_t field = 0;
			for (;;) {
				struct Setting::FieldAccess const *const access = &Setting::sensor_fields[sensor][field].access;
				if (!access->size) break;
				print->print(access->write(p));
				print->write(',');
				p = pointer_offset<void const, void const>(p, access->size);
				++field;
			}
		}
	}

	print->write('\n');
}

bool Data::readln(class Stream *const stream) {
	{
		/* read time */
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

	{
		/* read fields of all active sensors */
		void *p = pointer_offset<void, union Data>(this, offset[1]);
		for (unsigned int sensor = 1; sensor < Setting::num_of_sensors; ++sensor) {
			if (Setting::active_sensors[sensor].isJust()) {
				size_t field = 0;
				for (;;) {
					struct Setting::FieldAccess const *const access = &Setting::sensor_fields[sensor][field].access;
					if (!access->size) break;
					class String const s = stream->readStringUntil(',');
					if (!access->read(p, s.c_str())) return false;
					p = pointer_offset<void, void>(p, access->size);
					++field;
				}
			}
		}
	}

	stream->readStringUntil('\n');
	return true;
}

void Data::println(void) const {
	COM::print("Time: ");
	Display::println(String(*this->get_time()));

	/* print values across all fields of all active sensors */
	void const *p = pointer_offset<void const, union Data const>(this, offset[1]);
	for (unsigned int sensor = 1; sensor < Setting::num_of_sensors; ++sensor) {
		if (Setting::active_sensors[sensor].isJust()) {
			size_t field = 0;
			for (;;) {
				struct Setting::SensorField const *const sensor_field = &Setting::sensor_fields[sensor][field];
				if (!sensor_field->access.size) break;
				Display::print(sensor_field->label);
				Display::print(": ");
				Display::print(sensor_field->access.write(p));
				Display::println(sensor_field->unit);
				p = pointer_offset<void const, void const>(p, sensor_field->access.size);
				++field;
			}
		}
	}
}

void Data::dashboard(void) const {
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

	static unsigned int sensor = 0;
	static unsigned int field = 0;
	static size_t offset = 0;

	struct Setting::SensorField const *const sensor_field = &Setting::sensor_fields[sensor][field];
	if (!sensor) {
		OLED::println("Every");
		OLED::print(Variable::measure_interval / 60000., 2);
		OLED_space_or_newline();
		OLED::println("min.");
	}
	else {
		OLED::println(sensor_field->label);
		OLED::print(sensor_field->access.write(pointer_offset<void const, union Data const>(this, offset)));
		OLED_space_or_newline();
		OLED::println(sensor_field->unit);
	}
	offset += sensor_field->access.size;
	++field;
	while (!Setting::active_sensors[sensor].isJust() || !Setting::sensor_fields[sensor][field].access.size) {
		field = 0;
		++sensor;
		if (sensor >= Setting::num_of_sensors) {
			sensor = 0;
			offset = 0;
			break;
		}
	}
}

/* ************************************************************************** */

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
			if (Setting::active_sensors[Setting::Dallas].isJust()) {
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
			if (Setting::active_sensors[Setting::SHT40].isJust()) {
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
			if (Setting::active_sensors[Setting::BME280].isJust()) {
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
			if (Setting::active_sensors[Setting::LTR390].isJust()) {
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

	bool measure(union Data *const data) {
		DEVICE_LOCK(device_lock);
		if (!RTC::now(data->get_time()))
			return false;

		#if defined(ENABLE_BATTERY_GAUGE_LC709203F) || defined(ENABLE_BATTERY_GAUGE_MAX17043)
			if (Setting::active_sensors[Setting::battery].isJust()) {
				enum Setting::battery const type =
					static_cast<enum Setting::battery>(Setting::active_sensors[Setting::battery].unwrap());
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
			if (Setting::active_sensors[Setting::Dallas].isJust()) {
				float *const value = data->device_pointer<float>(Setting::Dallas);
				dallas.requestTemperatures();
				delay(750);
				*value = dallas.getTempCByIndex(0);
			}
		#endif

		#if defined(ENABLE_SHT40)
			if (Setting::active_sensors[Setting::SHT40].isJust()) {
				float *const values = data->device_pointer<float>(Setting::SHT40);
				sensors_event_t temperature_event, humidity_event;
				SHT.getEvent(&humidity_event, &temperature_event);
				values[0] = correct_SHT_temperature(temperature_event.temperature);
				values[1] = humidity_event.relative_humidity;
			}
		#endif

		#if defined(ENABLE_BME280)
			if (Setting::active_sensors[Setting::BME280].isJust()) {
				float *const values = data->device_pointer<float>(Setting::BME280);
				values[0] = BME.readTemperature();
				values[1] = BME.readPressure();
				values[2] = BME.readHumidity();
			}
		#endif

		#if defined(ENABLE_LTR390)
			if (Setting::active_sensors[Setting::LTR390].isJust()) {
				float *const value = data->device_pointer<float>(Setting::Dallas);
				*value = LTR.readUVS();
			}
		#endif

		return true;
	}
}

/* ************************************************************************** */
