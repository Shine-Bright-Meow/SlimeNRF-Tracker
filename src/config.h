#ifndef SLIMENRF_CONFIG
#define SLIMENRF_CONFIG

#include "retained.h"

// wrap nvs/retain read/write
// nvs SETTINGS_ID
// retain retained->settings
// config_settings_id is the word offset

// ops: read/write/reset

// config 0 and 1 are boolean flags
// config 2 is word array

// read bool (config 0): id, read OVRD_0 from retain, read CONFIG_0_DEVICE_CONFIG from retain if needed, return
// read bool (config 1): id
// read int16 (config 2): id
// read int32 (config 3): id

// w
//memcpy(retained->settings + addr, &val, 2);
//sys_write(SETTINGS_ID, NULL, retained->settings, sizeof(retained->settings));
// r
//memcpy(&val, retained->settings + addr * 2, 2);

#define CONFIG_SETTINGS_COUNT 4

// map of settings area
struct config_settings_data {
    uint16_t config_0_ovrd; // Override enable flags
    uint16_t config_0_settings;
    uint16_t config_1_ovrd;
    uint16_t config_1_settings;
    uint16_t config_2_ovrd;
    int16_t config_2_settings[16];
    uint16_t config_3_ovrd;
    int32_t config_3_settings[16];
};

// because settings read is a macro, need to expose settings struct and defaults
extern struct config_settings_data *config_settings;

// device settings
enum config_0_settings_id {
    CONFIG_0_USER_EXTRA_ACTIONS,
    CONFIG_0_IGNORE_RESET,
    CONFIG_0_USER_SHUTDOWN,
    CONFIG_0_USE_IMU_WAKEUP,
    CONFIG_0_DELAY_SLEEP_ON_STATUS,
    CONFIG_0_CONNECTION_OVER_HID,
    CONFIG_0_SETTINGS_END
};

// sensor settings
enum config_1_settings_id {
    CONFIG_1_SENSOR_USE_LOW_POWER_2,
    CONFIG_1_USE_IMU_TIMEOUT,
    CONFIG_1_USE_ACTIVE_TIMEOUT,
    CONFIG_1_SENSOR_USE_MAG,
    CONFIG_1_USE_SENSOR_CLOCK,
    CONFIG_1_SENSOR_USE_6_SIDE_CALIBRATION,
    CONFIG_1_SETTINGS_END
};

enum config_2_settings_id {
    CONFIG_2_LED_DEFAULT_COLOR_R,
    CONFIG_2_LED_DEFAULT_COLOR_G,
    CONFIG_2_LED_DEFAULT_COLOR_B,
    CONFIG_2_ACTIVE_TIMEOUT_MODE, // 0: Sleep, 1: Shutdown
    CONFIG_2_SENSOR_ACCEL_ODR,
    CONFIG_2_SENSOR_GYRO_ODR,
    CONFIG_2_SENSOR_ACCEL_FS,
    CONFIG_2_SENSOR_GYRO_FS,
    CONFIG_2_SENSOR_FUSION, // 1: XIO, 3: VQF
    CONFIG_2_RADIO_TX_POWER,
    CONFIG_2_SETTINGS_END
};

enum config_3_settings_id {
    CONFIG_3_CONNECTION_TIMEOUT_DELAY,
    CONFIG_3_SENSOR_LP_TIMEOUT,
    CONFIG_3_IMU_TIMEOUT_RAMP_MIN,
    CONFIG_3_IMU_TIMEOUT_RAMP_MAX,
    CONFIG_3_ACTIVE_TIMEOUT_THRESHOLD,
    CONFIG_3_ACTIVE_TIMEOUT_DELAY,
    CONFIG_3_SETTINGS_END
};

extern const uint16_t config_settings_count[];

extern const char *config_settings_names[];

// because settings read is a macro, need to expose settings struct and defaults
extern const bool config_0_settings_defaults[16];
extern const bool config_1_settings_defaults[16];
extern const int16_t config_2_settings_defaults[16];
extern const int32_t config_3_settings_defaults[16];

#define CONFIG_0_SETTINGS_READ(id) ((config_settings->config_0_ovrd & (1 << id)) ? (config_settings->config_0_settings & (1 << id)) : (config_0_settings_defaults[id]))
#define CONFIG_1_SETTINGS_READ(id) ((config_settings->config_1_ovrd & (1 << id)) ? (config_settings->config_1_settings & (1 << id)) : (config_1_settings_defaults[id]))
#define CONFIG_2_SETTINGS_READ(id) ((config_settings->config_2_ovrd & (1 << id)) ? (config_settings->config_2_settings[id]) : (config_2_settings_defaults[id]))
#define CONFIG_3_SETTINGS_READ(id) ((config_settings->config_3_ovrd & (1 << id)) ? (config_settings->config_3_settings[id]) : (config_3_settings_defaults[id]))

//bool config_0_settings_read(uint16_t id);
//bool config_1_settings_read(uint16_t id);
//int16_t config_2_settings_read(uint16_t id);
//int32_t config_3_settings_read(uint16_t id);

void config_0_settings_write(uint16_t id, bool value);
void config_1_settings_write(uint16_t id, bool value);
void config_2_settings_write(uint16_t id, int16_t value);
void config_3_settings_write(uint16_t id, int32_t value);

void config_settings_reset(uint16_t config, uint16_t id);
void config_settings_reset_all(void);

#endif

/*
menu "Status LED default status color"
    depends on LED_RGB_COLOR

config LED_DEFAULT_COLOR_R
    int "Red value"
    range 0 10000
    default 4000
    depends on LED_RGB_COLOR

config LED_DEFAULT_COLOR_G
    int "Green value"
    range 0 10000
    default 6000
    depends on LED_RGB_COLOR

config LED_DEFAULT_COLOR_B
    int "Blue value"
    range 0 10000
    default 0
    depends on LED_RGB_COLOR

endmenu

config USER_EXTRA_ACTIONS
    bool "Multiple press actions"

config IGNORE_RESET
    bool "Ignore reset"
    default y

config USER_SHUTDOWN
    bool "User shutdown support"
    default y

config USE_IMU_WAKE_UP
    bool "IMU wake up support"
    default y

config DELAY_SLEEP_ON_STATUS
    bool "Delay IMU wake up mode on status flags"
    default y
    depends on USE_IMU_WAKE_UP

config WOM_USE_DCDC
    bool "Use DCDC in IMU wake up mode"
    depends on USE_IMU_WAKE_UP

config CONNECTION_TIMEOUT_DELAY
    int "Connection timeout delay (ms)"
    default 300000
    depends on USER_SHUTDOWN

menu "Sensor power saving"

config SENSOR_LP_TIMEOUT
    int "Sensor low power delay (ms)"
    default 500

config SENSOR_USE_LOW_POWER_2
    bool "Use additional low power modes"

config USE_IMU_TIMEOUT
    bool "Use IMU wake up state"
    default y
    depends on USE_IMU_WAKE_UP

config IMU_TIMEOUT_RAMP_MIN
    int "Sensor timeout minimum delay (ms)"
    default 5000
    depends on USE_IMU_TIMEOUT || SENSOR_USE_LOW_POWER_2

config IMU_TIMEOUT_RAMP_MAX
    int "Sensor timeout maximum delay (ms)"
    default 15000
    depends on USE_IMU_TIMEOUT || SENSOR_USE_LOW_POWER_2

config USE_ACTIVE_TIMEOUT
    bool "Use activity timeout"
    default y
    depends on USE_IMU_WAKE_UP || USER_SHUTDOWN

choice
store as single int instead!
    prompt "Activity timeout mode"
    default SLEEP_ON_ACTIVE_TIMEOUT
    depends on USE_ACTIVE_TIMEOUT

config SLEEP_ON_ACTIVE_TIMEOUT
    bool "IMU wake up"
    depends on USE_IMU_WAKE_UP

config SHUTDOWN_ON_ACTIVE_TIMEOUT
    bool "User shutdown"
    depends on USER_SHUTDOWN

endchoice

config ACTIVE_TIMEOUT_THRESHOLD
    int "Activity timeout threshold (ms)"
    default 15000
    depends on USE_ACTIVE_TIMEOUT

config ACTIVE_TIMEOUT_DELAY
    int "Activity timeout delay (ms)"
    default 900000
    depends on USE_ACTIVE_TIMEOUT

endmenu

config SENSOR_ACCEL_ODR
    int "Accelerometer output data rate (Hz)"
    default 100

config SENSOR_GYRO_ODR
    int "Gyrometer output data rate (Hz)"
    default 200

config SENSOR_ACCEL_FS
    int "Accelerometer full scale (g)"
    default 4

config SENSOR_GYRO_FS
    int "Gyrometer full scale (dps)"
    default 1000

config SENSOR_USE_MAG
    bool "Magnetometer support"
    default y

config USE_SENSOR_CLOCK
    bool "Use external IMU clock"
    default y

choice
store as single int instead!
	prompt "Sensor fusion"
    default SENSOR_USE_VQF

config SENSOR_USE_XIOFUSION
    bool "Use x-io Technologies Fusion"

config SENSOR_USE_VQF
    bool "Use VQF"

endchoice

config SENSOR_USE_6_SIDE_CALIBRATION
    bool "Use 6-side calibration"
    default y
    depends on USE_SLIMENRF_CONSOLE

config RADIO_TX_POWER
    int "Radio output power (dBm)"
    default 8

config CONNECTION_OVER_HID
    bool "Use HID for data output"
    default n

*/

/*
schema:
storage is all in a 16bit array for ints
second binary array for bools

can address the config system by writing/reading words
for bools, similar but use 0/1? or true/false?

this has to be stored efficiently in retain, probably fixed array sizes
for nvs, the size can be variable depending on how many settings are known to the device
*/


/*
menu "Status LED default status color"
    depends on LED_RGB_COLOR

config LED_DEFAULT_COLOR_R
    int "Red value"
    range 0 10000
    default 4000
    depends on LED_RGB_COLOR

config LED_DEFAULT_COLOR_G
    int "Green value"
    range 0 10000
    default 6000
    depends on LED_RGB_COLOR

config LED_DEFAULT_COLOR_B
    int "Blue value"
    range 0 10000
    default 0
    depends on LED_RGB_COLOR

endmenu

config USER_EXTRA_ACTIONS
    bool "Multiple press actions"

config IGNORE_RESET
    bool "Ignore reset"
    default y

config USER_SHUTDOWN
    bool "User shutdown support"
    default y

config USE_IMU_WAKE_UP
    bool "IMU wake up support"
    default y

config DELAY_SLEEP_ON_STATUS
    bool "Delay IMU wake up mode on status flags"
    default y
    depends on USE_IMU_WAKE_UP

config WOM_USE_DCDC
    bool "Use DCDC in IMU wake up mode"
    depends on USE_IMU_WAKE_UP

config CONNECTION_TIMEOUT_DELAY
    int "Connection timeout delay (ms)"
    default 300000
    depends on USER_SHUTDOWN

menu "Sensor power saving"

config SENSOR_LP_TIMEOUT
    int "Sensor low power delay (ms)"
    default 500

config SENSOR_USE_LOW_POWER_2
    bool "Use additional low power modes"

config USE_IMU_TIMEOUT
    bool "Use IMU wake up state"
    default y
    depends on USE_IMU_WAKE_UP

config IMU_TIMEOUT_RAMP_MIN
    int "Sensor timeout minimum delay (ms)"
    default 5000
    depends on USE_IMU_TIMEOUT || SENSOR_USE_LOW_POWER_2

config IMU_TIMEOUT_RAMP_MAX
    int "Sensor timeout maximum delay (ms)"
    default 15000
    depends on USE_IMU_TIMEOUT || SENSOR_USE_LOW_POWER_2

config USE_ACTIVE_TIMEOUT
    bool "Use activity timeout"
    default y
    depends on USE_IMU_WAKE_UP || USER_SHUTDOWN

choice
store as single int instead!
    prompt "Activity timeout mode"
    default SLEEP_ON_ACTIVE_TIMEOUT
    depends on USE_ACTIVE_TIMEOUT

config SLEEP_ON_ACTIVE_TIMEOUT
    bool "IMU wake up"
    depends on USE_IMU_WAKE_UP

config SHUTDOWN_ON_ACTIVE_TIMEOUT
    bool "User shutdown"
    depends on USER_SHUTDOWN

endchoice

config ACTIVE_TIMEOUT_THRESHOLD
    int "Activity timeout threshold (ms)"
    default 15000
    depends on USE_ACTIVE_TIMEOUT

config ACTIVE_TIMEOUT_DELAY
    int "Activity timeout delay (ms)"
    default 900000
    depends on USE_ACTIVE_TIMEOUT

endmenu

config SENSOR_ACCEL_ODR
    int "Accelerometer output data rate (Hz)"
    default 100

config SENSOR_GYRO_ODR
    int "Gyrometer output data rate (Hz)"
    default 200

config SENSOR_ACCEL_FS
    int "Accelerometer full scale (g)"
    default 4

config SENSOR_GYRO_FS
    int "Gyrometer full scale (dps)"
    default 1000

config SENSOR_USE_MAG
    bool "Magnetometer support"
    default y

config USE_SENSOR_CLOCK
    bool "Use external IMU clock"
    default y

choice
store as single int instead!
	prompt "Sensor fusion"
    default SENSOR_USE_VQF

config SENSOR_USE_XIOFUSION
    bool "Use x-io Technologies Fusion"

config SENSOR_USE_VQF
    bool "Use VQF"

endchoice

config SENSOR_USE_6_SIDE_CALIBRATION
    bool "Use 6-side calibration"
    default y
    depends on USE_SLIMENRF_CONSOLE

config RADIO_TX_POWER
    int "Radio output power (dBm)"
    default 8

config CONNECTION_OVER_HID
    bool "Use HID for data output"
    default n

*/

/*
write/read to settings area performed as a write/read of arbitrary bytes (lol)
write byte <hex> <int>
write word <hex> <int>
write all <base64>
read byte <hex> -> <int>
read word <hex> -> <int>
read all -> <base64>
*/

/*
i    led default color r/g/b
b    user extra actions
b    ignore reset
b    user shutdown
b    use imu wakeup
b    delay sleep on status
i    connection timeout delay
i    lp timeout
b    use lp2
b    use imu timeout
i    imu timeout ramp min
i    imu timeout ramp max
b    use active timeout
i    sleep/shutdown on active timeout
i    active timeout threshold
i    active timeout delay
i    sensor accel odr
i    sensor gyro odr
i    sensor accel fs
i    sensor gyro fs
b    use mag
b    ?use sensor clock
i    xiofusion/vqf
b    6 side
i    radio tx power
b    connection over hid
*/
