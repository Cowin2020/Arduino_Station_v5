#include <ctime>
#include <cstdio>

#include "config_device.h"
#include "variable.h"
#include "display.h"
#include "SDCard.h"

#include "basic.h"

/* ************************************************************************** */

unsigned int parse_uint(char const **const next) {
	unsigned int x = 0;
	for (char const *s = *next; *s; ++s) {
		char const c = *s;
		if (c < '0' || c > '9') {
			*next = s;
			return x;
		}
		x = (x * 10) + (c - '0');
	}
}

FullTime::operator String(void) const {
	char buffer[24];
	snprintf(
		buffer, sizeof buffer,
		"%04u-%02u-%02uT%02u:%02u:%02uZ",
		this->year, this->month, this->day,
		this->hour, this->minute, this->second
	);
	return String(buffer);
}

struct FullTime &FullTime::operator +=(signed int const timezone_hours) {
	struct tm time = {
		.tm_sec = this->second,
		.tm_min = this->minute,
		.tm_hour = this->hour,
		.tm_mday = this->day,
		.tm_mon = this->month - 1,
		.tm_year = this->year - 1900,
		.tm_isdst = false
	};
	time_t epoch = mktime(&time);
	epoch += timezone_hours * 3600;
	gmtime_r(&epoch, &time);
	this->year = time.tm_year + 1900;
	this->month = time.tm_mon + 1;
	this->day = time.tm_mday;
	this->hour = time.tm_hour;
	this->minute = time.tm_min;
	this->second = time.tm_sec;
	return *this;
}

Configuration::Configuration(void) : measure_interval(0) {}

bool Configuration::decode(class String const &string) {
	for (char const *p = string.c_str();; ++p)
		switch (*p) {
			case 0:
				return true;
			case ' ':
			case '\t':
			case '\v':
			case '\r':
			case '\n':
				break;
			case 'm': {
				++p;
				unsigned int const value = parse_uint(&p);
				if (*p != '.')
					return false;
				measure_interval = value;
				break;
			}
			default:
				return false;
		}
}

void Configuration::apply(void) const {
	if (
		measure_interval != Variable::measure_interval
			&& measure_interval > (2 + RESEND_TIMES) * SEND_INTERVAL
			&& measure_interval <= 1000*60*60*24
	) {
		if (Variable::measure_interval != measure_interval) {
			Variable::measure_interval = measure_interval;
			SDCard::write_config();
		}
	}
}

/* ************************************************************************** */
