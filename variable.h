#ifndef INCLUDE_VARIABLE_H
#define INCLUDE_VARIABLE_H

/* ************************************************************************** */

#include <stdbool.h>

#include <WString.h>

#include "basic.h"

/* ************************************************************************** */

namespace Variable {
	extern bool const enable_gateway;
	extern bool const enable_measure;

	extern Device device_id;
	extern unsigned char secret_key[16];
	extern class String wifi_ssid;
	extern class String wifi_pass;
	extern class String http_authorization_type;
	extern class String http_authorization_code;
	extern class String site_name;
	extern Millisecond measure_interval;

	extern bool set_from_strings(String key, String value);
}

/* ************************************************************************** */

#endif // INCLUDE_VARIABLE_H
