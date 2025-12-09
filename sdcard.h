#ifndef INCLUDE_SDCARD_H
#define INCLUDE_SDCARD_H

#include "device.h"

/* ************************************************************************** */

namespace SDCard {
	extern void write_config(void);
	extern void read_config(void);
	extern bool clean_up(void);
	extern void add_data(union NewData const *data);
	extern bool read_data(union NewData *data);
	extern void next_data(void);
	extern bool initialize(void);
}

/* ************************************************************************** */

#endif // INCLUDE_SDCARD_H
