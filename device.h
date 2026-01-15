#ifndef INCLUDE_DEVICE_H
#define INCLUDE_DEVICE_H

#include <mutex>
#include <optional>
#include <map>

#include <NTPClient.h>
#include <WiFi.h>

#include "config_device.h"
#include "basic.h"

/* ************************************************************************** */

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
		battery = 1,
		Dallas  = 2,
		SHT40   = 3,
		BME280  = 4,
		LTR390  = 5,
		num_of_sensors
	};
	enum battery {
		LC709203F = 1,
		MAX17043  = 2,
		MAX17048  = 3
	};

	extern class std::optional<unsigned int> active_sensors[num_of_sensors];
	extern char const *const *const upload_names[];
	struct FieldAccess {
		size_t size;
		bool (*read)(void *memory, char const *string);
		class String (*write)(void const *memory);
	};
	extern struct SensorField {
		struct FieldAccess access;
		char const *label;
		char const *unit;
	} const *const sensor_fields[];

	extern void save(std::map<unsigned int, unsigned int> *map);
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

union Data {
private:
	struct FullTime time;

public:
	static size_t total_size;
	static size_t offset[Setting::num_of_sensors];

	static void initialize(void);

	template <typename T>
	inline T const *device_pointer(unsigned int const sensor) const {
		return pointer_offset<T const, union Data const>(this, offset[sensor]);
	}
	template <typename T>
	inline T *device_pointer(unsigned int const sensor) {
		return pointer_offset<T, union Data>(this, offset[sensor]);
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

namespace Sensor {
	extern bool initialize(void);
	extern bool measure(union Data *data);
}

/* ************************************************************************** */

#endif // INCLUDE_DEVICE_H
