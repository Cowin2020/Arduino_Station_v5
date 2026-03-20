#include <atomic>
#include <mutex>

#include <SD.h>

#include "variable.h"
#include "display.h"
#include "sdcard.h"

#define CONFIG_FILE_PATH "/CONFIG.INI"
#define DATA_FILE_PATH "/BUFFER.CSV"
#define CLEANUP_FILE_PATH "/CLEANUP.CSV"
#define LOG_FILE_PATH "/DATALOG.CSV"
#define ERROR_FILE_PATH_LENGTH 16
#define ERROR_FILE_PATH_PATTERN "/ERROR%03u.CSV"
#include "config_device.h"

/* ************************************************************************** */

namespace SDCard {
	#if defined(ENABLE_SDCARD)
		static char const config_file_path[] = CONFIG_FILE_PATH;
		static char const data_file_path[] = DATA_FILE_PATH;
		static char const cleanup_file_path[] = CLEANUP_FILE_PATH;
		#if defined(ENABLE_LOG_FILE)
			static char const log_file_path[] = LOG_FILE_PATH;
		#endif
		static class SPIClass SPI_1(HSPI);
		static off_t current_position = 0;
		static off_t next_position = 0;

		void write_config(void) {
			class File file = SD.open(config_file_path, "w");
			if (!file) {
				COM::println("Cannot open config file to save");
				return;
			}
			try {
				Variable::dump_to_stream(&file);
			}
			catch (...) {
				OLED::println("Cannot write config file");
			}
		}

		void create_new_config(void) {
			if (!SD.exists(config_file_path))
				write_config();
		}

		void read_config(void) {
			class File file = SD.open(config_file_path, "r");
			if (!file) {
				Display::println("No config file");
				return;
			}
			try {
				for (;;) {
					class String s = file.readStringUntil('\n');
					if (!s.length()) break;
					if (s[s.length()-1] == '\r') s.remove(s.length()-1);
					if (!s.length()) break;
					if (s[s.length()-1] == '\n') s.remove(s.length()-1);
					if (!s.length()) break;
					int const e = s.indexOf('=');
					if (e < 0 || s.length() <= e) continue;
					class String k = s.substring(0, e);
					class String v = s.substring(e+1, s.length());
					k.trim();
					v.trim();
					Debug::print("DEBUG: SDCard::read_config ");
					Debug::print(k);
					Debug::print('=');
					Debug::println(v);
					Variable::set_from_strings(k, v);
				}
				Display::println("Config file is read");
			}
			catch (...) {
				Display::println("Cannot read config file");
			}
			file.close();
		}

		static unsigned int count_files(void) {
			File root = SD.open("/");
			if (!root || !root.isDirectory()) return 0;
			unsigned int count = 0;
			for (;;) {
				File entry = root.openNextFile();
				if (!entry) break;
				entry.close();
				++count;
			}
			return count;
		}

		static void clean_up_failed(void) {
			unsigned int const num_of_files = count_files();
			char filename[ERROR_FILE_PATH_LENGTH];
			snprintf(filename, sizeof filename, ERROR_FILE_PATH_PATTERN, num_of_files);
			SD.rename(cleanup_file_path, filename);
			current_position = 0;
			next_position = 0;
		}

		bool clean_up(void) {
			if (!Variable::enable_measure) return false;
			DEVICE_LOCK(device_lock);
			Display::println("Cleaning up data file");
			OLED::display();
			COM::flush();
			if (SD.exists(cleanup_file_path))
				SD.remove(data_file_path);
			else if (!SD.rename(data_file_path, cleanup_file_path))
				return false;
			class File cleanup_file = SD.open(cleanup_file_path, "r");
			if (!cleanup_file) {
				COM::println("Fail to open clean-up file");
				clean_up_failed();
				return false;
			}
			class File data_file = SD.open(data_file_path, "w");
			if (!data_file) {
				COM::println("Fail to create data file");
				cleanup_file.close();
				clean_up_failed();
				return false;
			}

			#if !defined(DEBUG_CLEAN_DATA)
				for (;;) {
					class String const s = cleanup_file.readStringUntil(',');
					if (!s.length()) break;

					char memory[Data::total_size];
					union Data *data = reinterpret_cast<union Data *>(memory);
					if (!(s == "0" || s == "1") || !data->readln(&cleanup_file)) {
						COM::println("WARN: SDCard::clean_up: invalid data");
						cleanup_file.close();
						data_file.close();
						clean_up_failed();
						return false;
					}

					if (s == "0") {
						data_file.print("0,");
						data->writeln(&data_file);
					}
				}
			#endif

			cleanup_file.close();
			data_file.close();
			current_position = 0;
			next_position = 0;
			SD.remove(cleanup_file_path);
			return true;
		}

		void add_data(union Data const *const data) {
			if (!Variable::enable_measure) return;
			DEVICE_LOCK(device_lock);
			class File data_file = SD.open(data_file_path, "a");
			if (!data_file)
				Display::println("Cannot open data file");
			else {
				try {
					data_file.print("0,");
					data->writeln(&data_file);
				}
				catch (...) {
					Display::println("Cannot append data file");
				}
				data_file.close();
			}
			#if defined(ENABLE_LOG_FILE)
				class File log_file = SD.open(log_file_path, "a");
				if (!log_file) {
					OLED_LOCK(oled_lock);
					Display::println("Cannot open log file");
				}
				else {
					try {
						if (!log_file.position()) {
							log_file.print("Time (local),Time (UTC),");
							for (unsigned int sensor = 1; sensor < Setting::num_of_sensors; ++sensor)
								if (Setting::active_sensors[sensor].has_value()) {
									unsigned int field = 0;
									for (;;) {
										struct Setting::SensorField const *const sf = &Setting::sensor_fields[sensor][field];
										if (!sf->access.size) break;
										log_file.print(sf->description);
										log_file.write(',');
										++field;
									}
								}
							log_file.println();
						}
						struct FullTime local_time = *data->get_time();
						local_time += Variable::local_timezone;
						log_file.print(data->get_time()->to_local_string());
						log_file.write(',');
						data->writeln(&log_file);
					}
					catch (...) {
						OLED_LOCK(oled_lock);
						Display::println("Cannot append log file");
					}
					log_file.close();
				}
			#endif
		}

		bool read_data(union Data *const data) {
			if (!Variable::enable_measure) return false;
			DEVICE_LOCK(device_lock);
			class File file = SD.open(DATA_FILE_PATH, "r+", true);
			if (!file) {
				COM::println("ERROR: SDCard::read_data failed to open data file");
				return false;
			}
			if (!file.seek(current_position)) {
				COM::print("ERROR: SDCard::read_data could not seek to ");
				COM::println(current_position);
				file.close();
				return false;
			}
			bool success = false;
			for (;;) {
				class String const s = file.readStringUntil(',');
				if (!s.length()) break;
				bool const sent = s != "0";
				if (s != "0" && s != "1") {
					COM::print("ERROR: SDCard::read_data invalid flag at ");
					COM::println(file.position());
					break;
				}
				if (!data->readln(&file)) {
					COM::print("ERROR: SDCard::read_data invalid data at ");
					COM::println(file.position());
					break;
				}
				next_position = file.position();
				if (s == "0") {
					success = true;
					break;
				}
				current_position = next_position;
			}
			file.close();
			return success;
		}

		void next_data(void) {
			{
				DEBUG_LOCK(debug_lock);
				Debug::print("DEBUG: SDCard::next_data current_position=");
				Debug::print(current_position);
				Debug::print(" next_position=");
				Debug::println(next_position);
				Debug::flush();
			}
			if (!Variable::enable_measure) return;
			if (current_position == next_position) return;
			DEVICE_LOCK(device_lock);
			class File file = SD.open(DATA_FILE_PATH, "r+", true);
			if (!file) {
				COM::println("ERROR: SDCard::next_data failed to open data file");
				return;
			}
			if (!file.seek(current_position)) {
				COM::println("ERROR: SDCard::next_data failed to seek data file");
				return;
			}
			file.write('1');
			file.close();
			current_position = next_position;
		}

		bool initialize(void) {
			pinMode(SD_MISO, INPUT_PULLUP);
			SPI_1.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
			if (SD.begin(SD_CS, SPI_1)) {
				{
					OLED_LOCK(oled_lock);
					Display::println("SD card initialized");
					COM::println(String("SD Card type: ") + String(SD.cardType()));
				}
				return true;
			}
			else {
				OLED_LOCK(oled_lock);
				Display::println("SD card uninitialized");
				OLED::display();
				return false;
			}
		}
	#else
		void write_config(void) {}
		void create_new_config(void) {}
		void read_config(void) {}
		bool clean_up(void) {return false;}
		void add_data(union Data const *const data) {}
		bool read_data(union Data *const data) {return false;}
		void next_data(void) {}
		bool initialize(void) {return true;}
	#endif
}

/* ************************************************************************** */
