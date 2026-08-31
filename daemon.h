#ifndef INCLUDE_DAEMON_H
#define INCLUDE_DAEMON_H

/*
This module contains a group of processes (daemons) that execute in intervals
of time.  There is a system to trace the sleep time of the processes and puts
the CPU into sleep mode.
*/

/* ************************************************************************** */

#include <atomic>
#include <condition_variable>

#include <esp_pthread.h>

#include "basic.h"

/* ************************************************************************** */

namespace DAEMON {
	extern void thread_delay(Millisecond ms);
	struct Alarm {
		std::mutex mutex;
		std::condition_variable condition_variable;
		std::atomic<bool> wake;
		std::atomic<bool> sleepless;
		void notify(void);
	};
	namespace Schedule {
		extern void add_timer(struct Alarm *timer_alarm, char const *name);
		extern void remove_timer(struct Alarm *timer_alarm);
	}
	namespace LoRa {
		[[noreturn]] extern void loop(void);
	}
	namespace Time {
		extern void run(void);
		[[noreturn]] extern void loop(void);
	}
	namespace AskTime {
		extern void synchronized(void);
		[[noreturn]] extern void loop(void);
	}
	namespace Push {
		bool initialize(void);
		extern void ack(SerialNumber serial);
		[[noreturn]] void loop(void);
	}
	namespace Dashboard {
		[[noreturn]] extern void loop(void);
	}
	namespace Measure {
		[[noreturn]] extern void loop(void);
	}
	extern void run(void);
	extern bool initialize(void);
}

/* ************************************************************************** */

#endif // INCLUDE_DAEMON_H
