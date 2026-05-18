#include <RNG.h>
#include <WiFi.h>

#include "variable.h"
#include "display.h"
#include "device.h"
#include "inet.h"
#include "comm.h"
#include "sdcard.h"
#include "daemon.h"

/* ************************************************************************** */

#include "config_id.h"
#include "config_device.h"

/* ************************************************************************** */

static bool setup_success;

#if defined(CPU_FREQUENCY)
	static void set_CPU_frequency(void) {
		unsigned long int CPU_frequency = CPU_FREQUENCY;
		if (CPU_frequency < 20)
			CPU_frequency = 20;
		#if defined(ENABLE_OLED_OUTPUT)
			else if (CPU_frequency < 24)
				CPU_frequency = 24;
		#endif
		else if (Variable::enable_gateway && CPU_frequency < 80)
			CPU_frequency = 80;
		setCpuFrequencyMhz(CPU_frequency);
	}
#else
	inline static void set_CPU_frequency(void) {}
#endif

static void show_variables(void) {
	#if defined(ENABLE_OLED_OUTPUT)
		OLED::print("Dev ");
		OLED::println(Variable::device_id);
		OLED::display();
	#endif
	#ifdef ENABLE_COM_OUTPUT
		Variable::dump_to_stream(&Serial);
	#endif
}

void setup(void) {
	#if !defined(NDEBUG) && defined(START_DELAY)
		delay(START_DELAY);
	#endif

	setup_success = false;
	LED::initialize();
	COM::initialize();
	OLED::initialize();

	if (!SDCard::initialize()) {
		show_variables();
		goto end;
	}
	Setting::save(&Variable::active_sensors);
	SDCard::read_config();
	Setting::load(&Variable::active_sensors);
	Setting::save(&Variable::active_sensors);
	SDCard::create_new_config();
	show_variables();

	Data::initialize();
	if (SDCard::clean_up())
		Display::println("Data file cleaned");

	set_CPU_frequency();
	if (!RTC::initialize()) goto end;
	if (!Sensor::initialize()) goto end;
	WIFI::initialize();
	if (!LORA::initialize()) goto end;
	if (!DAEMON::initialize()) goto end;
	#if defined(ENABLE_OLED_SWITCH)
		pinMode(ENABLE_OLED_SWITCH, INPUT_PULLDOWN);
	#endif

	DAEMON::run();
	setup_success = true;
end:
	OLED::display();
}

void loop(void) {
	if (!setup_success) {
		LED::flash();
		return;
	}
	try {
		WIFI::loop();
		#if defined(REBOOT_TIMEOUT)
			if (millis() > REBOOT_TIMEOUT)
				esp_restart();
		#endif
		RNG.loop();
		DAEMON::thread_delay(IDLE_INTERVAL);
	}
	catch (...) {
		COM::println("ERROR: loop exception thrown");
	}
}
