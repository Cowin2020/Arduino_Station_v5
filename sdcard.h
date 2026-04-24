#ifndef INCLUDE_SDCARD_H
#define INCLUDE_SDCARD_H

#include "device.h"

/* ************************************************************************** */

namespace SDCard {
	extern void write_config(void);
	extern void read_config(void);
	extern void create_new_config(void);
	extern bool clean_up(void);
	extern void add_data(class Data const *data);
	extern bool read_data(class Data *data);
	extern void next_data(void);
	extern bool initialize(void);
}

/* ************************************************************************** */

#endif // INCLUDE_SDCARD_H
