#include "variable.h"
#include "config_device.h"
#include "display.h"

/* ************************************************************************** */

namespace Variable {
	char secret_key[16] = SECRET_KEY;
	class String wifi_ssid = WIFI_SSID;
	class String wifi_pass = WIFI_PASS;
	class String site_name = SITE_NAME;

	bool set_from_strings(String const key, String const value) {
		if (key == "SECRET_KEY") {
			memset(Variable::secret_key, 0, sizeof Variable::secret_key);
			for (unsigned int i = 0; i < value.length(); ++i) {
				unsigned int const j = i % sizeof Variable::secret_key;
				Variable::secret_key[j] =
					Variable::secret_key[j]
						^ (Variable::secret_key[j] << 1)
						^ value[i];
			}
		}
		else if (key == "WIFI_SSID")
			Variable::wifi_ssid = value;
		else if (key == "WIFI_PASS")
			Variable::wifi_pass = value;
		else if (key == "SITE_NAME")
			Variable::site_name = value;
		else {
			COM::print("Unknown config key ");
			COM::print(key);
			COM::print('=');
			COM::println(value);
		}
	}
}

/* ************************************************************************** */
