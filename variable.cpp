#include <climits>

#include "variable.h"

#include "config_id.h"
#include "config_device.h"
#include "display.h"

/* ************************************************************************** */

#if defined(ENABLE_GATEWAY)
	#if DEVICE_ID != 0
		#error "DEVICE_ID should be 0 for gateway"
	#endif
#else
	#if DEVICE_ID == 0
		#error "DEVICE_ID should not be 0 other than gateway"
	#endif
#endif

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
	class String http_authorization_type = HTTP_AUTHORIZATION_TYPE;
	class String http_authorization_user = HTTP_AUTHORIZATION_USER;
	class String http_authorization_pass = HTTP_AUTHORIZATION_PASS;
	class String http_upload_host = HTTP_UPLOAD_HOST;
	class String http_upload_path_base = HTTP_UPLOAD_PATH_BASE;
	class String http_upload_field_site = HTTP_UPLOAD_FILED_SITE;
	class String http_upload_field_device = HTTP_UPLOAD_FILED_DEVICE;
	class String http_upload_field_serial = HTTP_UPLOAD_FILED_SERIAL;
	class String http_upload_field_time = HTTP_UPLOAD_FILED_TIME;
	class String site_code = SITE_CODE;
	Millisecond measure_interval = MEASURE_INTERVAL;
	bool enable_sleep =
		#if defined(ENABLE_SLEEP) && !defined(ENABLE_GATEWAY)
			true
		#else
			false
		#endif
		;

	std::map<unsigned int, unsigned int> active_sensors;

	void dump_to_stream(class Print *const stream) {
		stream->print("DEVICE_ID=");
		stream->println(device_id);
		{
			char ascii_key[sizeof secret_key << 1];
			for (size_t i = 0; i < sizeof secret_key; ++i) {
				ascii_key[i] = (secret_key[i] >> 4) | 0x40;
				ascii_key[i + sizeof secret_key] = ((secret_key[i] >> 4) ^ (secret_key[i] & 0x0F)) | 0x40;
			}
			stream->print("SECRET_KEY=");
			for (size_t i = 0; i < sizeof ascii_key; ++i)
				stream->print(ascii_key[i]);
			stream->println();
		}
		stream->print("WIFI_SSID=");
		stream->println(wifi_ssid);
		stream->print("WIFI_PASS=");
		stream->println(wifi_pass);
		stream->print("HTTP_AUTHORIZATION_TYPE=");
		stream->println(http_authorization_type);
		stream->print("HTTP_AUTHORIZATION_USER=");
		stream->println(http_authorization_user);
		stream->print("HTTP_AUTHORIZATION_PASS=");
		stream->println(http_authorization_pass);
		stream->print("SITE_CODE=");
		stream->println(site_code);
		stream->print("MEASURE_INTERVAL/minute=");
		stream->println(measure_interval / (1000*60));
		stream->print("ACTIVE_SENSORS=");
		for (std::map<unsigned int, unsigned int>::value_type pair : active_sensors) {
			stream->print(pair.first);
			if (pair.second != UINT_MAX) {
				stream->print(':');
				stream->print(pair.second);
			}
			stream->print(',');
		}
		stream->println();
	}

	bool set_from_strings(class String const key, class String const value) {
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
		else if (key == "HTTP_AUTHORIZATION_TYPE")
			http_authorization_type = value;
		else if (key == "HTTP_AUTHORIZATION_USER")
			http_authorization_user = value;
		else if (key == "HTTP_AUTHORIZATION_PASS")
			http_authorization_pass = value;
		else if (key == "SITE_CODE")
			site_code = value;
		else if (key == "MEASURE_INTERVAL/minute") {
			char const *p = value.c_str();
			unsigned int const n = parse_uint(&p) * (1000*60);
			if (*p || n <= SEND_INTERVAL || n >= 1000*60*60*24) {
				COM::print("WARN: Incorrect measure interval ");
				COM::println(value);
				return false;
			}
			measure_interval = n;
		}
		else if (key == "ACTIVE_SENSORS") {
			active_sensors.clear();
			unsigned int n = 0;
			char const *p = value.c_str();
			for (;;) {
				char const c = *(p++);
				if (c == ' ') ;
				else if (c >= '0' && c <= '9')
					n = n * 10 + (c - '0');
				else if (!c || c == ',') {
					if (n) {
						Debug::print("DEBUG: Variable::set_from_strings active_sensors[");
						Debug::print(n);
						Debug::println("] = UINT_MAX");
						active_sensors[n] = UINT_MAX;
						n = 0;
					}
					if (!c) break;
				}
				else if (c == ':' && n) {
					unsigned int m = 0;
					for (;;) {
						char const c = *(p++);
						if (c >= '0' && c <= '9')
							m = m * 10 + (c - '0');
						else if (!c || c == ',') {
							Debug::print("DEBUG: Variable::set_from_strings active_sensors[");
							Debug::print(n);
							Debug::print("] = ");
							Debug::println(m);
							active_sensors[n] = m;
							break;
						}
						else {
							COM::print("WARN: Incorrect sensor value ");
							COM::println(value);
							break;
						}
					}
					n = 0;
					if (!c) break;
				}
				else {
					COM::print("WARN: Incorrect active sensors ");
					COM::println(value);
					break;
				}
			}
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
