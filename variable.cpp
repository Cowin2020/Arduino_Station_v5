#include "variable.h"

#include "config_id.h"
#include "config_device.h"
#include "display.h"

/* ************************************************************************** */

namespace Variable {
	Device device_id = DEVICE_ID;
	bool const enable_gateway =
		#if defined(ENABLE_GATEWAY)
			true
		#else
			false
		#endif
		;
	bool const enable_measure =
		#if defined(ENABLE_MEASURE)
			true
		#else
			false
		#endif
		;
	unsigned char secret_key[16] = SECRET_KEY;
	class String wifi_ssid = WIFI_SSID;
	class String wifi_pass = WIFI_PASS;
	class String site_name = SITE_NAME;
	extern Millisecond measure_interval = MEASURE_INTERVAL;

	bool set_from_strings(String const key, String const value) {
		if (key == "DEVICE_ID" && !enable_gateway) {
			char const *p = value.c_str();
			unsigned int const n = parse_uint(&p);
			if (*p || n <= 0 || n > 255) {
				COM::print("WARN: Incorrect device ID ");
				COM::println(value);
				return false;
			}
			device_id = n;
		}
		else if (key == "SECRET_KEY") {
			memset(secret_key, 0, sizeof secret_key);
			for (unsigned int i = 0; i < value.length(); ++i) {
				unsigned int const j = i % sizeof secret_key;
				secret_key[j] = secret_key[j] ^ (secret_key[j] << 4) ^ value[i];
			}
		}
		else if (key == "WIFI_SSID")
			wifi_ssid = value;
		else if (key == "WIFI_PASS")
			wifi_pass = value;
		else if (key == "SITE_NAME")
			site_name = value;
		else if (key == "MEASURE_INTERVAL") {
			char const *p = value.c_str();
			unsigned int const n = parse_uint(&p);
			if (*p || n < 60 || n > 3600) {
				COM::print("WARN: Incorrect measure interval ");
				COM::println(value);
				return false;
			}
			measure_interval = n;
		}
		else {
			COM::print("WARN: Unknown config key ");
			COM::print(key);
			COM::print('=');
			COM::println(value);
			return false;
		}
		return true;
	}
}

/* ************************************************************************** */
