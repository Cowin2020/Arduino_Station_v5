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

void setup(void) {
	#if !defined(NDEBUG) && defined(START_DELAY)
		delay(START_DELAY);
	#endif
	setup_success = false;
	if (CPU_frequency) setCpuFrequencyMhz(CPU_frequency);
	LED::initialize();
	COM::initialize();
	OLED::initialize();
	if (!SDCard::initialize()) goto end;
	SDCard::read_config();
	Setting::load(&Variable::active_devices);
	NewData::initialize();
	if (SDCard::clean_up())
		Display::println("Data file cleaned");
	if (!RTC::initialize()) goto end;
	if (!Sensor::initialize()) goto end;
	WIFI::initialize();
	if (!LORA::initialize()) goto end;
	if (!DAEMON::initialize()) goto end;
	DAEMON::run();
	#if defined(ENABLE_OLED_SWITCH)
		pinMode(ENABLE_OLED_SWITCH, INPUT_PULLDOWN);
	#endif
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
		vTaskDelay(pdMS_TO_TICKS(IDLE_INTERVAL));
		//	delay(IDLE_INTERVAL);
	}
	catch (...) {
		COM::println("ERROR: loop exception thrown");
	}
}
