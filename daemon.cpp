#include <limits>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include <RNG.h>

#include "id.h"
#include "display.h"
#include "device.h"
#include "lora.h"
#include "sdcard.h"
#include "inet.h"
#include "daemon.h"

/* ************************************************************************** */

#if !defined(SEND_INTERVAL)
	#define SEND_INTERVAL (ACK_TIMEOUT * (RESEND_TIMES + 2))
#endif

static bool const enable_sleep =
	#if defined(ENABLE_SLEEP)
		!enable_gateway
	#else
		false
	#endif
	;

template <typename TYPE>
static TYPE rand_int(void) {
	TYPE x;
	RNG.rand((uint8_t *)&x, sizeof x);
	return x;
}

/* ************************************************************************** */

namespace DAEMON {
	static esp_pthread_cfg_t esp_pthread_cfg;

	void thread_delay(Millisecond const ms) {
		//	TickType_t const ticks = ms / portTICK_PERIOD_MS;
		TickType_t const ticks = pdMS_TO_TICKS(ms);
		vTaskDelay(ticks>0 ? ticks : 1);
		//	std::this_thread::sleep_for(std::chrono::duration<Millisecond, std::milli>(ms));
	}

	static void yield(void) {
		vTaskDelay(1);
		//	taskYIELD();
		//	sched_yield();
		//	std::this_thread::yield();
	}

	void Alarm::notify(void) {
		awake.store(true);
		condition_variable.notify_all();
	}

	namespace Schedule {
		struct Timer {
			struct Alarm *alarm;
			Millisecond start, duration;
			#if !defined(NDEBUG)
				class String name;
			#endif
		};

		static struct Alarm alarm;
		static std::vector<struct Timer> timer_list;
		static std::mutex timer_mutex;

		void add_timer(struct Alarm *const timer_alarm, char const *const name) {
			std::lock_guard<std::mutex> lock(timer_mutex);
			struct Timer const timer {
				.alarm = timer_alarm,
				.start = 0,
				.duration = 0
				#if !defined(NDEBUG)
					,
					.name = String(name)
				#endif
			};
			timer_list.push_back(timer);
		}

		void remove_timer(struct Alarm *const timer_alarm) {
			std::lock_guard<std::mutex> lock(timer_mutex);
			for (size_t i = 0; i < timer_list.size(); ++i)
				if (timer_list[i].alarm == timer_alarm) {
					timer_list[i] = timer_list[timer_list.size()-1];
					timer_list.pop_back();
					break;
				}
			alarm.notify();
		}

		static void sleep(struct Alarm *const timer_alarm, Millisecond const duration) {
			{
				Millisecond now = millis();
				std::lock_guard<std::mutex> lock(timer_mutex);
				for (struct Timer &timer: timer_list)
					if (timer.alarm == timer_alarm) {
						timer.start = now;
						timer.duration = duration;
						goto bed;
					}
				COM::println("ERROR: DAEMON::Schedule::sleep unregistered condition");
				return;
			}
		bed:
			timer_alarm->awake.store(false);
			alarm.notify();
			std::unique_lock<std::mutex> lock(timer_alarm->mutex);
			timer_alarm->condition_variable.wait(lock, [timer_alarm] {return timer_alarm->awake.load();});
		}

		void loop(void) {
			thread_delay(12345);
			for (;;)
				try {
					alarm.awake.store(false);
					bool wake = false;
					struct Timer const *soonest = nullptr;
					Millisecond const now = millis();
					{
						std::lock_guard<std::mutex> lock(timer_mutex);
						for (struct Timer &timer: timer_list) {
							if (!timer.duration)
								wake = true;
							else if (timer.duration <= now - timer.start) {
								timer.duration = 0;
								timer.alarm->awake.store(true);
								timer.alarm->condition_variable.notify_all();
								wake = true;
							}
							else if (
								(soonest == nullptr && timer.duration > 0) ||
								timer.start + timer.duration - now < soonest->start + soonest->duration - now
							)
								soonest = &timer;
						}
					}
					if (!wake && soonest != nullptr) {
						Millisecond duration = soonest->start + soonest->duration - now;
						if (enable_sleep && duration > SLEEP_MARGIN) {
							DEVICE_LOCK(device_lock);
							Debug::flush();
							LORA::sleep();
							esp_sleep_enable_timer_wakeup(1000 * (duration - SLEEP_MARGIN));
							esp_light_sleep_start();
							LORA::wake();
							yield();
						}
						else
							thread_delay(duration);
						continue;
					}
					std::unique_lock<std::mutex> lock(alarm.mutex);
					alarm.condition_variable.wait(lock, [] {return alarm.awake.load();});
				}
				catch (...) {
					COM::println("ERROR: DAEMON::Schedule::loop exception thrown");
				}
		}
	}

	namespace LoRa {
		[[noreturn]]
		void loop(void) {
			for (;;)
				try {
					LORA::Receive::packet();
					yield();
				}
				catch (...) {
					COM::println("ERROR: DAEMON::LoRa::loop exception thrown");
				}
		}
	}

	namespace Time {
		static struct Alarm alarm;

		void run(void) {
			alarm.notify();
			yield();
		}

		[[noreturn]]
		void loop(void) {
			Schedule::add_timer(&alarm, "DAEMON::Time");
			for (;;)
				try {
					struct FullTime fulltime;
					if (NTP::now(&fulltime)) {
						RTC::set(&fulltime);
						LORA::Send::TIME(&fulltime);

						OLED_LOCK(oled_lock);
						OLED::home();
						Display::print("Synchronize: ");
						Display::println(String(fulltime));
						OLED::display();
					}
					Schedule::sleep(&alarm, SYNCHONIZE_INTERVAL);
				}
				catch (...) {
					COM::println("ERROR: DAEMON::Time::loop exception thrown");
				}
		}
	}

	namespace AskTime {
		static std::atomic<Millisecond> last_synchronization(0);
		static struct Alarm alarm;

		void synchronized(void) {
			last_synchronization = millis();
		}

		[[noreturn]]
		void loop(void) {
			Schedule::add_timer(&alarm, "DAEMON::AskTime");
			thread_delay(SYNCHONIZE_TIMEOUT);
			LORA::Send::ASKTIME();
			thread_delay(SYNCHONIZE_TIMEOUT);
			for (;;)
				try {
					LORA::Send::ASKTIME();
					thread_delay(SYNCHONIZE_TIMEOUT);
					Schedule::sleep(&alarm, SYNCHONIZE_INTERVAL - SYNCHONIZE_TIMEOUT + rand_int<uint8_t>());
				}
				catch (...) {
					COM::println("ERROR: DAEMON::AskTime::loop exception thrown");
				}
		}
	}

	namespace Push {
		static struct Alarm alarm;
		static std::atomic<SerialNumber> current_serial(0);
		static std::atomic<SerialNumber> acked_serial(0);
		static std::atomic<bool> send_success;

		static void send_data(struct Data const data) {
			if (enable_gateway) {
				struct WIFI::upload__result const upload_result =
					WIFI::upload(my_device_id, ++current_serial, &data);
				if (upload_result.upload_success) {
					send_success.store(true);
					#if defined(DASHBOARD_INTERVAL) && DASHBOARD_INTERVAL > 0
						{
							OLED_LOCK(oled_lock);
							OLED::draw_received();
						}
					#endif
					if (upload_result.update_configuration)
						upload_result.configuration.apply();
				}
				else {
					COM::print("HTTP unable to send data: time=");
					COM::println(String(data.time));
				}
			}
			else {
				/* TODO: add routing */
				for (unsigned int t=0;;) {
					LORA::Send::SEND(my_device_id, ++current_serial, &data);
					Schedule::sleep(&alarm, ACK_TIMEOUT);
					if (acked_serial.load() == current_serial.load()) {
						send_success.store(true);
						SDCard::next_data();
						break;
					}
					Debug::print("DEBUG: DAEMON::Push::send_data t=");
					Debug::println(t);
					if (t >= RESEND_TIMES) break;
					Schedule::sleep(&alarm, SEND_INTERVAL);
					++t;
				}
			}
		}

		void data(struct Data const *const data) {
			SDCard::add_data(data);
		}

		void ack(SerialNumber const serial) {
			acked_serial.store(serial);
		}

		[[noreturn]]
		void loop(void) {
			Schedule::add_timer(&alarm, "DAEMON::Push");
			Schedule::sleep(&alarm, START_DELAY);
			for (;;)
				try {
					struct Data data;
					if (SDCard::read_data(&data)) {
						send_success.store(false);
						esp_pthread_set_cfg(&esp_pthread_cfg);
						send_data(data);
					}
					else
						send_success.store(true);
					#if SEND_IDLE_INTERVAL > SEND_INTERVAL
						if (!send_success.load())
							Schedule::sleep(&alarm, SEND_IDLE_INTERVAL);
						else
					#endif
							Schedule::sleep(&alarm, SEND_INTERVAL);
				}
				catch (...) {
					COM::println("ERROR: DAEMON::Push::loop exception thrown");
				}
		}
	}

	namespace CleanLog {
		static struct Alarm alarm;

		void loop(void) {
			Schedule::add_timer(&alarm, "DAEMON::CleanLog");
			for (;;)
				try {
					Schedule::sleep(&alarm, CLEANLOG_INTERVAL);
					SDCard::clean_up();
				}
				catch (...) {
					COM::println("ERROR: DAEMON::CleanData::loop exception thrown");
				}
		}
	}

	namespace Dashboard {
		static struct Alarm alarm;
		static struct Data data;
		static unsigned int state = 0;

		static void show(void) {
			OLED_LOCK(lock);
			OLED::home(0, 10);
			OLED::print("Dev");
			OLED::println(int(my_device_id));
			struct FullTime time = data.time;
			#if defined(DASHBOARD_TIMEZONE)
				time += DASHBOARD_TIMEZONE;
			#endif
			OLED::println(time.year);
			OLED::print(time.day);
			OLED::print('/');
			OLED::println(time.month);
			if (time.hour < 10) OLED::print('0');
			OLED::print(time.hour);
			OLED::print(':');
			if (time.minute < 10) OLED::print('0');
			OLED::println(time.minute);

			do {
				if (false) {
					/* NOTHING: unreachable */
				#if defined(ENABLE_BATTERY_GAUGE)
					}
					else if (state < __LINE__) {
						state = __LINE__;
						OLED::println("Power");
						OLED::println(data.battery_voltage, 1);
						OLED::print("V");
						break;
					}
					else if (state < __LINE__) {
						state = __LINE__;
						OLED::println("Power");
						OLED::println(data.battery_percentage, 0);
						OLED::print("%");
				#endif
				#if defined(ENABLE_DALLAS)
					}
					else if (state < __LINE__) {
						state = __LINE__;
						OLED::println("Dallas");
						OLED::println(data.dallas_temperature);
						OLED::print("deg C");
				#endif
				#if defined(ENABLE_SHT40)
					}
					else if (state < __LINE__) {
						state = __LINE__;
						OLED::println("SHT40");
						OLED::println(data.sht40_temperature);
						OLED::print("deg C");
					}
					else if (state < __LINE__) {
						state = __LINE__;
						OLED::println("SHT40");
						OLED::println(data.sht40_humidity);
						OLED::print("%RH");
				#endif
				#if defined(ENABLE_BME280)
					}
					else if (state < __LINE__) {
						state = __LINE__;
						OLED::println("BME280");
						OLED::println(data.bme280_temperature, 1);
						OLED::print("deg C");
					}
					else if (state < __LINE__) {
						state = __LINE__;
						OLED::println("BME280");
						OLED::println(data.bme280_pressure, 0);
						OLED::print("Pa");
					}
					else if (state < __LINE__) {
						state = __LINE__;
						OLED::println("BME280");
						OLED::println(data.bme280_humidity, 0);
						OLED::print("%RH");
				#endif
				#if defined(ENABLE_LTR390)
					}
					else if (state < __LINE__) {
						state = __LINE__;
						OLED::println("LTR");
						OLED::println(data.ltr390_ultraviolet);
						OLED::print("UV");
				#endif
				}
				else if (!state)
					break;
				else
					state = 0;
			}
			while (!state);
			if (Push::send_success.load())
				OLED::draw_received();
			OLED::display();
		}

		void loop(void) {
			Schedule::add_timer(&alarm, "DAEMON::Dashboard");
			Schedule::sleep(&alarm, MEASURE_INTERVAL + DASHBOARD_INTERVAL + START_DELAY);
			OLED::large_font();
			for (;;)
				try {
					show();
					Schedule::sleep(&alarm, DASHBOARD_INTERVAL);
				}
				catch (...) {
					COM::println("ERROR: DAEMON::Dashboard::loop exception thrown");
				}
		}
	}

	namespace Measure {
		static Millisecond interval = MEASURE_INTERVAL;
		static struct Alarm alarm;

		static void print_data(struct Data const *const data) {
			OLED_LOCK(lock);
			OLED::home();
			Display::print("Device ");
			Display::println(my_device_id);
			data->println();
			OLED::display();
		}

		void set_interval(Millisecond const ms) {
			interval = max((2 + RESEND_TIMES) * SEND_INTERVAL, ms);
		}

		[[noreturn]]
		void loop(void) {
			Schedule::add_timer(&alarm, "DAEMON::Measure");
			Schedule::sleep(&alarm, START_DELAY);
			for (;;)
				try {
					struct Data data;
					if (Sensor::measure(&data)) {
						Dashboard::data = data;
						#if !defined(DASHBOARD_INTERVAL) || !(DASHBOARD_INTERVAL > 0)
							print_data(&data);
						#endif
						Push::data(&data);
					}
					else
						COM::println("Failed to measure");
					Schedule::sleep(&alarm, interval);
				}
				catch (...) {
					COM::println("ERROR: DAEMON::Measure::loop exception thrown");
				}
		}
	}

	void run(void) {
		esp_pthread_cfg = esp_pthread_get_default_config();
		esp_pthread_cfg.stack_size = 4096;
		esp_pthread_cfg.inherit_cfg = true;

		esp_pthread_set_cfg(&esp_pthread_cfg);
		std::thread(LoRa::loop).detach();

		if (enable_gateway) {
			esp_pthread_set_cfg(&esp_pthread_cfg);
			std::thread(Time::loop).detach();
		}
		else {
			esp_pthread_set_cfg(&esp_pthread_cfg);
			std::thread(AskTime::loop).detach();
		}

		if (enable_measure) {
			esp_pthread_set_cfg(&esp_pthread_cfg);
			std::thread(Push::loop).detach();
			esp_pthread_set_cfg(&esp_pthread_cfg);
			#if defined(DASHBOARD_INTERVAL) && DASHBOARD_INTERVAL > 0
				std::thread(Dashboard::loop).detach();
				esp_pthread_set_cfg(&esp_pthread_cfg);
			#endif
			std::thread(Measure::loop).detach();
			esp_pthread_set_cfg(&esp_pthread_cfg);
			#if defined(ENABLE_SDCARD)
				std::thread(CleanLog::loop).detach();
			#endif
		}

		esp_pthread_set_cfg(&esp_pthread_cfg);
		std::thread(Schedule::loop).detach();
	}
}

/* ************************************************************************** */
