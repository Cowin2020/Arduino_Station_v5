Software for development
========================

Arduino IDE
-----------

https://www.arduino.cc/en/software

USB-Enhanced-SERIAL CH9102 COM port driver
------------------------------------------

Microsoft Windows
https://github.com/Xinyuan-LilyGO/CH9102_Driver

macOS
https://github.com/WCHSoftGroup/ch34xser_macos

Libraries used
==============

Additional Board Manager URLs:
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_dev_index.json

Boards: esp32 (tested versions 2.0.17 - 3.3.11)

Libraries: see "doc/LIBRARY.TXT"

Device identity
===============

Create "config_id.h" in the root directory of repository.
The file "config_id.h.example" is an example of "config_id.h".

DEVICE_ID
---------

ID of the arduino device

Each device should have a unique ID.

Type: Single byte integer
Values:
	0
		LoRa-WiFi gateway (optional measurement)
	1-255
		terminal measurement devices

Device configuration
====================

Create "config_device.h" in the root directory of repository.
The file "config_device.h.example" is an example of "config_device.h".

Please check "doc/CONFIG.TXT" for the options in config files.
