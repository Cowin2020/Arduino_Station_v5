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

Boards: esp32 (tested versions 2.0.17 - 3.3.4)

Crypto
------

For older version before version 0.4.0:
	Edit libraries/Crypto/src/AES.h
		remove the usage of "CRYPTO_AES_ESP32"
		always use "CRYPTO_AES_DEFAULT"

NTPClient
---------

Synchronize real-time clock from NTP server.

IDE version does not work.
It should be installed from github.
https://github.com/arduino-libraries/NTPClient.git

ESP32Time
---------

Handle date-time using internal clock

LoRa
----

Driver of LoRa

Adafruit SSD1306
----------------

Driver of SSD1306 OLED display

Grove - High Precision RTC
--------------------------

Driver of PCF85063TP Real-time clock

RTClib
------

Driver of DS1307 and DS3231 real-time clocks

Adafruit LC709203F
------------------

LC709203F battery gauge

DFRobot_MAX17043
----------------

MAX17043 battery gauge

OneWire
-------

Driver of 1-wire interface

It is used by dallas temperature sensor.

DallasTemperature
-----------------

Dallas 1-wire temperature sensor

Adafruit SHT4x Library
----------------------

SHT40 temperature and humidity sensor

Adafruit BME280 Library
-----------------------

BME280 I2C temperature, pressure and humidity sensor

Adafruit LTR390 Library
-----------------------

LTR390 I2C ambient/ultraviolet light sensor

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

ENABLE_GATEWAY, ENABLE_MEASURE
------------------------------

Working mode

Type: one or both defined

ENABLE_GATEWAY
	This device is used as LoRa gateway
ENABLE_MEASURE
	This device will measure data

NDEBUG
------

Skip debug codes.

Type: defined or undefined

DEBUG_CLEAN_DATA
----------------

Clean all data on SD card at boot-up.

Type: defined or undefined

LOG_DATA
--------

All measured data will be saved in log file on SD card.

Type: defined or undefined

NUMBER_OF_DEVICES
-----------------

Number of devices, including all gateway and measurment devices

Values: 1-256

ENABLE_MEASURE
--------------

Measure data

Type: defined or undefined

Should be defined for devices 1-255, and optional for device 0.

ENABLE_SDCARD
--------------

SD card is installed and used to store measurement data

Type: defined or undefined

ENABLE_LOG_FILE
---------------

Record all measured data in log file

Type defined or undefined

ENABLE_CLOCK_PCF85063TP, ENABLE_CLOCK_DS1307, ENABLE_CLOCK_DS3231
-----------------------------------------------------------------

Real-time clock

Type: defined one of them or undefined at all

undefined
	Use internal clock only
ENABLE_CLOCK_PCF85063TP
	Grove high precision real-time clock
ENABLE_CLOCK_DS1307
	DS1307 real-time clock
ENABLE_CLOCK_DS3231
	DS3231 real-time clock

ENABLE_BATTERY_GAUGE_DFROBOT, ENABLE_BATTERY_GAUGE_LC709203F
------------------------------------------------------------

Battery gauge is used to monitor battery usage

Type: defined or undefined
	They can be both defined and then select one to use by
"DEFAULT_SENSOR_SETTING" or settings loaded from SD card.

ENABLE_BATTERY_GAUGE_MAX17043
	MAX17043 fuel gauge
ENABLE_BATTERY_GAUGE_LC709203F
	LC709203F battery monitor

ENABLE_DALLAS
-------------

Dallas thermometer is used

If defined, it represents the pin number of thermometer data wire.

Type: undefined or natural number

ENABLE_BME280
-------------

BME280 (temperature, pressure, humidity) sensor is used

Type: defined or undefined

ENABLE_LTR390
-------------

LTR390 I2C sensor is used

Type: defined or undefined

DEFAULT_SENSOR_SETTING
----------------------

Default settings of sensors

Type: arrary of unsigned int

The settings may be loaded from SD card.

SECRET_KEY
----------

Secret key for encryption of LoRa communication

Need to be same for both gateway and terminal sides.

Type: string or byte array with 16-byte size

ROUTER_TOPOLOGY
---------------

Setting of LoRa message routing

Circular routing should be avoided.

Type: array of pairs of device IDs (upstream device, downstream device).

Example: {{2, 1}, {0, 1}, {2, 4}, {4, 3}}

The topology will be:

     -------- [1]
    /        /
   /        /
[0] --- [2]
           \
            \
             [4] --- [3]

Device 1 sends data through device 2 or directly sends to device 0 (gateway device).
Device 4 only sends data through device 2, and device 3 only send through device 4.
Devices send only to device 0 by default if not listed.

LORA_BAND
---------

Radio frequency used for LoRa

Type: signed long int

WIFI_SSID and WIFI_PASS
-----------------------

WiFi access point and password

Type: C strings

NTP_SERVER
----------

NTP server that provides network clock

Type: C string

NTP_INTERVAL
------------

Milliseconds to (re)synchronize with network clock.

Type: natural number

HTTP_UPLOAD_LENGTH
---------------

Maximum length of HTTP URL string

Type: natural number

HTTP_AUTHORIZATION_TYPE, HTTP_AUTHORIZATION_CODE
------------------------------------------------

Authorization code on HTTP for uploading.

Type: C strings

HTTP_RESPONE_SIZE
-----------------

Buffer size for HTTP response from data server.

Type: natural number

SITE_CODE
---------

Identifier of the measurement site

HTTP_UPLOAD_FORMAT
------------------

Printf format string of URL to upload data

Type: C string

Position arguments:
	%1s
		site code, which defined by "SITE_CODE"
	%2$u
		device ID
	%3$u
		serial number of transmission
	%4$s
		date and time in ISO8601 format

HTTP_UPLOAD_FORMAT
------------------

A list of mapping HTTP keys of querystring and measured values

Type: array of arrays of C strings

The first index is Setting::sensor matches the sensors, and the second index
matches different values that measured by the sensor.

START_DELAY
-----------

Delay in milliseconds before starting first measure and send data.

Type: unsigned long int

IDLE_INTERVAL
-------------

Delay in milliseconds in the idle loop.
In the idle loop, internet (WiFi) status is checked.

Type: unsigned long int

ACK_TIMEOUT
-----------

Period in milliseconds to wait for ACK response

Type: unsigned long int

RESEND_TIMES
------------

Number of times to re-send data if ACK is not received

Type: unsigned int

SEND_INTERVAL
---------------

Milliseconds between re-send data

This value should be larger than (ACK_TIMEOUT * (RESEND_TIMES + 1)).

Type: unsigned long int
Suggested value: (ACK_TIMEOUT * (RESEND_TIMES + 2))

MEASURE_INTERVAL
----------------

Period in milliseconds to measure data

This value should be larger than SEND_INTERVAL.

Type: unsigned long int

SEND_IDLE_INTERVAL
------------------

Milliseconds to suspend sending LoRa data after failed RESEND_TIMES times

Type: unsigned long int

SYNCHONIZE_INTERVAL
-------------------

Period in milliseconds to synchonize real-time clock through LoRa

Type: unsigned long int

SYNCHONIZE_TIMEOUT
------------------

Milliseconds to wait the response of time synchonization

Type: unsigned long int

CLEANLOG_INTERVAL
-----------------

Period in milliseconds to clear data file on SD card

It can be undefined or set to zero to disable this function.

Type: undefined or unsigned long int

Suggestion: few hours to a day

SLEEP_MARGIN
------------

Number of milliseconds to wake up before measuring or sending data

Type: unsigned long int

REBOOT_TIMEOUT
--------------

Milliseconds to reboot

Type: undefined or unsigned long int

DASHBOARD_INTERVAL
------------------

Milliseconds to refresh the OLED display in dashboard mode

Type: undefined or unsigned long int

The dashboard mode is turned off if this variable is undefined.

DASHBOARD_SLEEP_INTERVAL
------------------------

Milliseconds to suspend dashboard refresh when OLED display is switched off

Type: unsigned long int

DASHBOARD_TIMEZONE
------------------

Time zone (in hours) that used on dashboard display

Type: signed int

Positive number for eastern hemisphere, and negative number for western hemisphere

ENABLE_LED
----------

Blink on board LED on error

Type: defined or undefined

ENABLE_COM_OUTPUT
-----------------

Output debug message to USB serial port

Type: defined or undefined

ENABLE_OLED_OUTPUT
------------------

Output message to OLED display

Type: defined or undefined

OLED_ROTATION
-------------

The rotation of the contents on OLED display

Type: integer between in [0, 3]

ENABLE_SLEEP
------------

Go to sleep mode when it is not sending or measuring data

Type: defined or undefined

CPU_FREQUENCY
-------------

Change CPU frequency to given value

Type: undefined or 240 or 160 or 80 or 40 or 20

LoRa works only on CPU frequency equal or greater than 20Hz.
WiFi needs 80Hz or higher.

COM_BAUD
--------

Bit rate of USB serial port

Type: positive number

OLED_WIDTH and OLED_HEIGHT
--------------------------

The dimension of OLED display

Type: positive numbers
