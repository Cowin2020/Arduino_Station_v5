#include <cstring>
#include <HTTPClient.h>

#include "variable.h"
#include "display.h"
#include "device.h"
#include "daemon.h"
#include "inet.h"

/* ************************************************************************** */

namespace WIFI {
	void initialize(void) {
		if (Variable::enable_gateway) {
			WiFi.mode(WIFI_STA);
			WiFi.begin(Variable::wifi_ssid, Variable::wifi_pass);
		}
		else {
			WiFi.mode(WIFI_OFF);
		}
	}

	static class String status_message(signed int const WiFi_status) {
		switch (WiFi_status) {
		case WL_NO_SHIELD:
			return String("WiFi no shield");
		case WL_IDLE_STATUS:
			return String("WiFi idle");
		case WL_NO_SSID_AVAIL:
			return String("WiFi no SSID");
		case WL_SCAN_COMPLETED:
			return String("WiFi scan completed");
		case WL_CONNECTED:
			return String("WiFi connected");
		case WL_CONNECT_FAILED:
			return String("WiFi connect failed");
		case WL_CONNECTION_LOST:
			return String("WiFi connection lost");
		case WL_DISCONNECTED:
			return String("WiFi disconnected");
		default:
			return String("WiFi Status: ") + String(WiFi_status);
		}
	}

	bool ready(void) {
		return WiFi.status() == WL_CONNECTED;
	}

	size_t build_URL_querystring(char *const buffer, union Data const *const data, Setting::sensor const sensor) {
		if (!Setting::active_sensors[sensor]) return 0;
		char const *const *name = Setting::upload_names[sensor];
		void const *value = data->device_pointer<void const>(sensor);
		Setting::SensorField const *field = Setting::sensor_fields[sensor];
		size_t p = 0;
		size_t n;
		while (field->access.size) {
			String const value_string = field->access.write(value);
			buffer[p++] = '&';
			n = std::strlen(*name);
			std::memcpy(buffer + p, *name, n);
			p += n;
			buffer[p++] = '=';
			n = value_string.length();
			std::memcpy(buffer + p, value_string.c_str(), n);
			p += n;
			++name;
			value = pointer_offset<void const, void const>(value, field->access.size);
			++field;
		}
		buffer[p] = '\0';
		return p;
	}

	struct upload__result upload(Device const device, SerialNumber const serial, union Data const *const data) {
		signed int const WiFi_status = WiFi.status();
		if (WiFi_status != WL_CONNECTED) {
			OLED_LOCK(oled_lock);
			Display::print("No WiFi: ");
			Display::println(status_message(WiFi.status()));
			return {.upload_success = false};
		}
		class String const time = String(*data->get_time());
		char URL[HTTP_UPLOAD_LENGTH];
		int p = snprintf(URL, sizeof URL, HTTP_UPLOAD_FORMAT, Variable::site_code.c_str(), device, serial, time.c_str());
		if (p < 0) {
			COM::println("ERROR: Unable to create HTTP URL from snprintf");
			return {.upload_success = false};
		}
		for (unsigned int sensor = 1; sensor < Setting::num_of_sensors; ++sensor)
			p += build_URL_querystring(URL + p, data, static_cast<enum Setting::sensor>(sensor));
		COM::print("Upload to ");
		COM::println(URL);
		class HTTPClient HTTP_client;
		HTTP_client.begin(URL);
		if (Variable::http_authorization_type.length() && Variable::http_authorization_code.length()) {
			HTTP_client.setAuthorizationType(Variable::http_authorization_type.c_str());
			HTTP_client.setAuthorization(Variable::http_authorization_code.c_str());
		}
		signed int const HTTP_status = HTTP_client.GET();
		{
			OLED_LOCK(oled_lock);
			Display::print("HTTP status: ");
			Display::println(HTTP_status);
		}
		if (!(HTTP_status >= 200 && HTTP_status < 300))
			return {.upload_success = false};
		if (HTTP_status != 200)
			return {.upload_success = true, .update_configuration = false};
		signed int const size = HTTP_client.getSize();
		if (size < 0 || size > HTTP_RESPONE_SIZE)
			return {.upload_success = true, .update_configuration = false};
		class Configuration configuration;
		if (!configuration.decode(HTTP_client.getString())) {
			COM::println("WARN: Configuration syntax error");
			return {.upload_success = true, .update_configuration = false};
		}
		return {.upload_success = true, .update_configuration = true, .configuration = configuration};
	}

	void loop(void) {
		static bool first_WiFi = false;
		static wl_status_t last_WiFi = WL_IDLE_STATUS;
		if (Variable::enable_gateway) {
			wl_status_t this_WiFi = WiFi.status();
			if (this_WiFi != last_WiFi) {
				COM::print("WiFi status: ");
				COM::println(status_message(WiFi.status()));
				if (this_WiFi == WL_CONNECTED && !first_WiFi) {
					first_WiFi = true;
					NTP::initialize();
				}
				switch (this_WiFi) {
					case WL_NO_SSID_AVAIL:
					case WL_CONNECT_FAILED:
					case WL_CONNECTION_LOST:
					case WL_DISCONNECTED:
						WiFi.begin(Variable::wifi_ssid, Variable::wifi_pass);
				}
				last_WiFi = this_WiFi;
			}
			if (this_WiFi == WL_CONNECTED)
				NTP::synchronize();
		}
	}
}

/* ************************************************************************** */
