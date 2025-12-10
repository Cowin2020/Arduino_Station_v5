#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include <RNG.h>

#include "variable.h"
#include "display.h"
#include "device.h"
#include "comm.h"
#include "sdcard.h"
#include "inet.h"
#include "daemon.h"

/* ************************************************************************** */

#if !defined(SEND_INTERVAL)
	#define SEND_INTERVAL (ACK_TIMEOUT * (RESEND_TIMES + 2))
#endif

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
		wake.store(true);
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
			timer_alarm->sleepless.store(false);
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
			timer_alarm->wake.store(false);
			alarm.notify();
			std::unique_lock<std::mutex> lock(timer_alarm->mutex);
			timer_alarm->condition_variable.wait(lock, [timer_alarm] {return timer_alarm->wake.load();});
		}

		static void idle(struct Alarm *const timer_alarm, Millisecond const duration) {
			timer_alarm->sleepless.store(true);
			sleep(timer_alarm, duration);
			timer_alarm->sleepless.store(false);
		}

		void loop(void) {
			thread_delay(START_DELAY + IDLE_INTERVAL);
			for (;;)
				try {
					alarm.wake.store(false);
					bool awake = false;
					struct Timer const *soonest = nullptr;
					bool sleepless = false;
					Millisecond const now = millis();
					{
						std::lock_guard<std::mutex> lock(timer_mutex);
						for (struct Timer &timer: timer_list) {
							if (timer.alarm->sleepless.load())
								sleepless = true;
							if (!timer.duration)
								awake = true;
							else if (timer.duration <= now - timer.start) {
								timer.duration = 0;
								timer.alarm->wake.store(true);
								timer.alarm->condition_variable.notify_all();
								awake = true;
							}
							else if (
								(soonest == nullptr && timer.duration > 0) ||
								timer.start + timer.duration - now < soonest->start + soonest->duration - now
							)
								soonest = &timer;
						}
					}
					if (!awake && soonest != nullptr) {
						Millisecond duration = soonest->start + soonest->duration - now;
						if (Variable::enable_sleep && !sleepless && duration > SLEEP_MARGIN) {
							DEVICE_LOCK(device_lock);
							Debug::print("DEBUG: sleep ");
							Debug::print(duration);
							Debug::println("ms");
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
					alarm.condition_variable.wait(lock, [] {return alarm.wake.load();});
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
		#if !defined(ENABLE_SDCARD)
			static std::mutex mutex;
			static bool filled = false;
			static union Data *last_data = nullptr;
		#endif

		bool initialize(void) {
			#if !defined(ENABLE_SDCARD)
				last_data = static_cast<union Data *>(malloc(Data::total_size));
				if (last_data == nullptr) {
					Display::println("No memory to store measurement data");
					return false;
				}
			#endif
			return true;
		}

		static void add_data(union Data const *const data) {
			#if defined(ENABLE_SDCARD)
				SDCard::add_data(data);
			#else
				if (last_data != nullptr) {
					std::lock_guard<std::mutex> lock(mutex);
					filled = true;
					std::memcpy(last_data, data, Data::total_size);
				}
			#endif
		}

		static bool read_data(union Data *const data) {
			#if defined(ENABLE_SDCARD)
				return SDCard::read_data(data);
			#else
				std::lock_guard<std::mutex> lock(mutex);
				if (!filled || last_data == nullptr) return false;
				std::memcpy(data, last_data, Data::total_size);
				return true;
			#endif
		}

		static void next_data(void) {
			#if defined(ENABLE_SDCARD)
				SDCard::next_data();
			#else
				std::lock_guard<std::mutex> lock(mutex);
				filled = false;
			#endif
		}

		static void send_data(union Data const *data) {
			if (Variable::enable_gateway) {
				struct WIFI::upload__result const upload_result =
					WIFI::upload(Variable::device_id, ++current_serial, data);
				if (upload_result.upload_success) {
					send_success.store(true);
					if (Variable::enable_measure) next_data();
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
					COM::println(String(*data->get_time()));
				}
			}
			else {
				/* TODO: add routing */
				for (unsigned int t = 0;;) {
					LORA::Send::SEND(Variable::device_id, ++current_serial, data);
					Schedule::idle(&alarm, ACK_TIMEOUT);
					if (acked_serial.load() == current_serial.load()) {
						send_success.store(true);
						next_data();
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

		void ack(SerialNumber const serial) {
			acked_serial.store(serial);
		}

		[[noreturn]]
		void loop(void) {
			Schedule::add_timer(&alarm, "DAEMON::Push");
			Schedule::sleep(&alarm, START_DELAY);
			for (;;)
				try {
					char memory[Data::total_size];
					union Data *const data = reinterpret_cast<union Data *>(memory);
					if (read_data(data)) {
						send_success.store(false);
						send_data(data);
						Debug::print("DEBUG: DAEMON::Push::loop send_success=");
						Debug::println((int)send_success.load());
						#if defined(ENABLE_SDCARD) && SEND_IDLE_INTERVAL > SEND_INTERVAL
							if (!send_success.load())
								Schedule::sleep(&alarm, SEND_IDLE_INTERVAL);
							else
						#endif
								Schedule::sleep(&alarm, SEND_INTERVAL);
					}
					else {
						#if defined(ENABLE_SDCARD)
							Schedule::sleep(&alarm, Variable::measure_interval);
						#else
							Schedule::sleep(&alarm, SEND_INTERVAL);
						#endif
					}
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
		static union Data *data = nullptr;

		#if defined(OLED_ROTATION) && !(OLED_ROTATION & 1)
			#define OLED_HORIZONAL
		#endif

		static void show(void) {
			OLED_LOCK(oled_lock);
			#if defined(OLED_HORIZONAL)
				OLED::home(0, 0);
			#else
				OLED::home(0, 10);
			#endif
			OLED::print("Dev");
			#if defined(OLED_HORIZONAL)
				OLED::print(' ');
			#endif
			data->dashboard();
			if (Push::send_success.load())
				OLED::draw_received();
			OLED::display();
		}

		static bool check_switch(void) {
			#if !defined(ENABLE_OLED_OUTPUT)
				return false;
			#elif defined(ENABLE_OLED_SWITCH)
				static bool switched_off = false;
				if (digitalRead(ENABLE_OLED_SWITCH) == LOW) {
					if (!switched_off) {
						Debug::println("DEBUG: OLED switch off");
						OLED::turn_off();
						switched_off = true;
					}
					return false;
				}
				else {
					if (switched_off) {
						Debug::println("DEBUG: OLED switch on");
						OLED::turn_on();
						switched_off = false;
					}
					return true;
				}
			#else
				return true;
			#endif
		}

		void loop(void) {
			#if defined(DASHBOARD_INTERVAL) && DASHBOARD_INTERVAL > 0
				char memory[Data::total_size];
				data = reinterpret_cast<union Data *>(memory);
				Schedule::add_timer(&alarm, "DAEMON::Dashboard");
				Schedule::sleep(&alarm, MEASURE_INTERVAL + DASHBOARD_INTERVAL + START_DELAY);
				#if !defined(OLED_HORIZONAL)
					OLED::large_font();
				#endif
				for (;;)
					try {
						if (check_switch()) {
							show();
							Schedule::sleep(&alarm, DASHBOARD_INTERVAL);
						}
						else {
							Schedule::sleep(&alarm, DASHBOARD_SLEEP_INTERVAL);
						}
					}
					catch (...) {
						COM::println("ERROR: DAEMON::Dashboard::loop exception thrown");
					}
			#else
				Display::println("ERROR: DAEMON::Dashboard::loop is executed");
			#endif
		}
	}

	namespace Measure {
		static struct Alarm alarm;

		static void print_data(union Data const *const data) {
			OLED_LOCK(oled_lock);
			OLED::home();
			Display::print("Device ");
			Display::println(Variable::device_id);
			data->println();
			OLED::display();
		}

		[[noreturn]]
		void loop(void) {
			char memory[Data::total_size];
			union Data *const data = reinterpret_cast<union Data *>(memory);
			Schedule::add_timer(&alarm, "DAEMON::Measure");
			Schedule::sleep(&alarm, START_DELAY);
			for (;;)
				try {
					if (Sensor::measure(data)) {
						#if defined(DASHBOARD_INTERVAL) && DASHBOARD_INTERVAL > 0
							if (Dashboard::data != nullptr)
								std::memcpy(Dashboard::data, data, Data::total_size);
						#else
							print_data(data);
						#endif
						Push::add_data(data);
					}
					else
						COM::println("Failed to measure");
					Schedule::sleep(&alarm, Variable::measure_interval);
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

		if (Variable::enable_gateway) {
			esp_pthread_set_cfg(&esp_pthread_cfg);
			std::thread(Time::loop).detach();
		}
		else {
			esp_pthread_set_cfg(&esp_pthread_cfg);
			std::thread(AskTime::loop).detach();
		}

		if (Variable::enable_measure) {
			esp_pthread_set_cfg(&esp_pthread_cfg);
			std::thread(Push::loop).detach();
			#if defined(DASHBOARD_INTERVAL) && DASHBOARD_INTERVAL > 0
				esp_pthread_set_cfg(&esp_pthread_cfg);
				std::thread(Dashboard::loop).detach();
			#endif
			esp_pthread_set_cfg(&esp_pthread_cfg);
			std::thread(Measure::loop).detach();
			#if defined(ENABLE_SDCARD)
				esp_pthread_set_cfg(&esp_pthread_cfg);
				std::thread(CleanLog::loop).detach();
			#endif
		}

		esp_pthread_set_cfg(&esp_pthread_cfg);
		std::thread(Schedule::loop).detach();
	}

	bool initialize(void) {
		if (!Push::initialize()) return false;
		return true;
	}
}

/* ************************************************************************** */
