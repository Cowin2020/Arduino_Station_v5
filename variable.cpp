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
	bool enable_gateway =
		#if defined(ENABLE_GATEWAY)
			true
		#else
			false
		#endif
		;
	bool enable_measure =
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
	class String http_upload_host_path = HTTP_UPLOAD_HOST_PATH;
	class String http_upload_query = HTTP_UPLOAD_QUERY;
	class String http_upload_field_site = HTTP_UPLOAD_FIELD_SITE;
	class String http_upload_field_device = HTTP_UPLOAD_FIELD_DEVICE;
	class String http_upload_field_serial = HTTP_UPLOAD_FIELD_SERIAL;
	class String http_upload_field_time = HTTP_UPLOAD_FIELD_TIME;
	Hour local_timezone = LOCAL_TIMEZONE;
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
		stream->print("OPERATION_MODE=");
		stream->println(
			(device_id || !enable_gateway)
				? "MEASURE"
				: enable_measure
					? "BOTH"
					: "GATEWAY"
		);
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
		stream->print("HTTP_UPLOAD_HOST_PATH=");
		stream->println(http_upload_host_path);
		stream->print("HTTP_UPLOAD_QUERY=");
		stream->println(http_upload_query);
		stream->print("HTTP_AUTHORIZATION_TYPE=");
		stream->println(http_authorization_type);
		stream->print("HTTP_AUTHORIZATION_USER=");
		stream->println(http_authorization_user);
		stream->print("HTTP_AUTHORIZATION_PASS=");
		stream->println(http_authorization_pass);
		stream->print("SITE_CODE=");
		stream->println(site_code);
		stream->print("LOCAL_TIMEZONE=");
		stream->println(static_cast<signed short int>(local_timezone));
		stream->print("MEASURE_INTERVAL/second=");
		stream->println(measure_interval / 1000);
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
		if (key == "DEVICE_ID") {
			char const *p = value.c_str();
			unsigned int const n = parse_uint(&p);
			if (*p || n > UCHAR_MAX) {
				COM::print("WARN: Incorrect device ID ");
				COM::println(value);
				return false;
			}
			device_id = n;
			enable_gateway = !n;
			if (n) enable_measure = true;
		}
		else if (key == "OPERATION_MODE") {
			if (value == "GATEWAY") {
				enable_gateway = true;
				enable_measure = false;
				device_id = 0;
			}
			else if (value == "MEASURE") {
				enable_gateway = false;
				enable_measure = true;
				if (!device_id) device_id = UCHAR_MAX;
			}
			else if (value == "BOTH") {
				enable_gateway = true;
				enable_measure = true;
				device_id = 0;
			}
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
		else if (key == "HTTP_UPLOAD_HOST_PATH")
			http_upload_host_path = value;
		else if (key == "HTTP_UPLOAD_QUERY")
			http_upload_query = value;
		else if (key == "HTTP_AUTHORIZATION_TYPE")
			http_authorization_type = value;
		else if (key == "HTTP_AUTHORIZATION_USER")
			http_authorization_user = value;
		else if (key == "HTTP_AUTHORIZATION_PASS")
			http_authorization_pass = value;
		else if (key == "SITE_CODE")
			site_code = value;
		else if (key == "LOCAL_TIMEZONE") {
			char const *p = value.c_str();
			unsigned int const n = parse_uint(&p) * 1000;
			if (*p || n <= -24 || n >= 24) {
				COM::print("WARN: Incorrect local timezone ");
				COM::println(value);
				return false;
			}
			local_timezone = n;
		}
		else if (key == "MEASURE_INTERVAL/second") {
			char const *p = value.c_str();
			unsigned int const n = parse_uint(&p) * 1000;
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
