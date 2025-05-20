#ifndef INCLUDE_SDCARD_H
#define INCLUDE_SDCARD_H

#include "device.h"

/* ************************************************************************** */

namespace SDCard {
	extern void read_config(void);
	extern bool clean_up(void);
	extern void add_data(struct Data const *data);
	extern bool read_data(struct Data *const data);
	extern void next_data(void);
	extern bool initialize(void);
}

/* ************************************************************************** */

#endif // INCLUDE_SDCARD_H
