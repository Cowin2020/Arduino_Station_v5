#ifndef INCLUDE_BASIC_H
#define INCLUDE_BASIC_H

/* ************************************************************************** */

#include <cstddef>
#include <cstdint>

#include <WString.h>

/* ************************************************************************** */

typedef unsigned long int Millisecond;

typedef uint8_t Device;
typedef uint32_t SerialNumber;

template<typename TO, typename FROM>
inline static TO *pointer_offset(FROM *const from, size_t const offset) {
	return reinterpret_cast<TO *>(reinterpret_cast<char *>(from) + offset);
}

template<typename TO, typename FROM>
inline static TO const *pointer_offset(FROM const *const from, size_t const offset) {
	return reinterpret_cast<TO const *>(reinterpret_cast<char const *>(from) + offset);
}

unsigned int parse_uint(char const **next);
extern size_t append_buffer(char *buffer, char const *string);
extern size_t append_buffer(char *buffer, class String const &string);

struct [[gnu::packed]] FullTime {
	unsigned short int year;
	unsigned char month;
	unsigned char day;
	unsigned char hour;
	unsigned char minute;
	unsigned char second;

	explicit operator String(void) const;
	struct FullTime &operator +=(signed int timezone_hours);
};

class Configuration {
public:
	unsigned int measure_interval;
	Configuration(void);
	bool decode(class String const &string);
	void apply(void) const;
};

template <typename TYPE>
class Maybe {
protected:
	TYPE value;
	bool occupy : 1;
public:
	inline Maybe(void) : occupy(false) {}
	inline Maybe(TYPE const x) : occupy(true), value(x) {}
	inline bool isNothing(void) const {return !occupy;}
	inline bool isJust(void) const {return occupy;}
	inline TYPE unwrap(void) const {return value;}
	inline bool operator==(TYPE const &x) {return occupy && value == x;}
};

/* ************************************************************************** */

#endif // INCLUDE_BASIC_H
