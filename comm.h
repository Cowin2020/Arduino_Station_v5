#ifndef INCLUDE_LORA_H
#define INCLUDE_LORA_H

/* ************************************************************************** */

#include "device.h"

/* ************************************************************************** */

namespace LORA {
	extern void confirm_receiver(void);
	extern bool next_receiver(void);
	extern bool initialize(void);
	extern void sleep(void);
	extern void wake(void);
	namespace Send {
		extern void TIME(bool repeating, struct FullTime const *fulltime);
		extern void ASKTIME(bool repeating, Device terminal_device);
		extern void SEND(SerialNumber serial, class Data const *data);
	}
	namespace Receive {
		extern void packet(void);
	}
}

/* ************************************************************************** */

#endif // INCLUDE_LORA_H
