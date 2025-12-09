#ifndef INCLUDE_DEVICE_H
#define INCLUDE_DEVICE_H

#include <mutex>
#include <map>

#include <NTPClient.h>
#include <WiFi.h>

#include "config_device.h"
#include "basic.h"

/* ************************************************************************** */

extern unsigned long int const CPU_frequency;

extern std::mutex device_mutex;

#define DEVICE_LOCK(VARIABLE) std::lock_guard<std::mutex> VARIALBE{device_mutex}

#if defined(ENABLE_OLED_OUTPUT)
	#define OLED_LOCK(VARIABLE) DEVICE_LOCK(VARIABLE)
#else
	#define OLED_LOCK(VARIABLE)
#endif

#if defined(NDEBUG)
	#define DEBUG_LOCK(VARIABLE)
#else
	#define DEBUG_LOCK(VARIABLE) DEVICE_LOCK(VARIABLE)
#endif

namespace Setting {
	enum sensor {
		battery       = 1,
		Dallas        = 2,
		SHT40         = 3,
		BME280        = 4,
		LTR390        = 5,
		num_of_sensors
	};
	enum battery {
		LC709203F = 1,
		MAX17043 = 2
	};

	extern unsigned int active_sensors[num_of_sensors];
	extern char const *const *const upload_names[];
	struct FieldAccess {
		size_t size;
		bool (*read)(void *memory, char const *string);
		class String (*write)(void const *memory);
	};
	extern struct SensorField {
		struct FieldAccess access;
		char const *label_long;
		char const *label_short;
		char const *unit;
	} const *const device_fields[];

	extern std::map<unsigned int, unsigned int> save(void);
	extern void load(std::map<unsigned int, unsigned int> const *map);
}

namespace RTC {
	extern bool initialize(void);
	extern void set(struct FullTime const* fulltime);
	extern bool now(struct FullTime *fulltime);
}

namespace NTP {
	extern void initialize(void);
	extern bool now(struct FullTime *fulltime);
	extern void synchronize(void);
}

union NewData {
private:
	struct FullTime time;

public:
	static void initialize(void);
	static size_t total_size;
	static size_t offset[Setting::num_of_sensors];

	template <typename T>
	inline T const *device_pointer(unsigned int const sensor) const {
		return pointer_offset<T const, union NewData const>(this, offset[sensor]);
	}
	template <typename T>
	inline T *device_pointer(unsigned int const sensor) {
		return pointer_offset<T, union NewData>(this, offset[sensor]);
	}
	inline struct FullTime const *get_time(void) const {
		return &this->time;
	}
	inline struct FullTime *get_time(void) {
		return &this->time;
	}
	void writeln(class Print *print) const;
	bool readln(class Stream *stream);
	void println(void) const;
	void dashboard(void) const;
};

struct [[gnu::packed]] Data {
	struct FullTime time;
	#ifdef ENABLE_BATTERY_GAUGE
		float battery_voltage;
		float battery_percentage;
	#endif
	#ifdef ENABLE_DALLAS
		float dallas_temperature;
	#endif
	#ifdef ENABLE_SHT40
		float sht40_temperature;
		float sht40_humidity;
	#endif
	#ifdef ENABLE_BME280
		float bme280_temperature;
		float bme280_pressure;
		float bme280_humidity;
	#endif
	#ifdef ENABLE_LTR390
		float ltr390_ultraviolet;
	#endif

	void writeln(class Print *print) const;
	bool readln(class Stream *stream);
	void println(void) const;
	void dashboard(void) const;
};

namespace Sensor {
	extern bool initialize(void);
	extern bool measure(union NewData *data);
}

/* ************************************************************************** */

#endif // INCLUDE_DEVICE_H
