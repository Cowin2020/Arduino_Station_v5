#include "variable.h"
#include "config_device.h"

/* ************************************************************************** */

namespace Variable {
	char secret_key[16] = SECRET_KEY;
	class String wifi_ssid = WIFI_SSID;
	class String wifi_pass = WIFI_PASS;
	class String site_name = SITE_NAME;
}

/* ************************************************************************** */
