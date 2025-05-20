#ifndef INCLUDE_VARIABLE_H
#define INCLUDE_VARIABLE_H

#include <WString.h>

/* ************************************************************************** */

namespace Variable {
	extern char secret_key[16];
	extern class String wifi_ssid;
	extern class String wifi_pass;
	extern class String site_name;

	extern bool set_from_strings(String key, String value);
}

/* ************************************************************************** */

#endif // INCLUDE_VARIABLE_H
