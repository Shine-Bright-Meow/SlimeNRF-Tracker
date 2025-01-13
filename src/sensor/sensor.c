/*
	SlimeVR Code is placed under the MIT license
	Copyright (c) 2025 SlimeVR Contributors

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in
	all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
	THE SOFTWARE.
*/
#include "../globals.h"
#include "../system/system.h"
#include "../util.h"
#include "../connection/connection.h"
#include "calibration.h"

#include <math.h>

#include "fusion/fusions.h"
#include "sensors.h"

#include "sensor.h"

#if DT_NODE_EXISTS(DT_NODELABEL(imu))
#define SENSOR_IMU_EXISTS true
#define SENSOR_IMU_NODE DT_NODELABEL(imu)
static struct i2c_dt_spec sensor_imu_dev = I2C_DT_SPEC_GET(SENSOR_IMU_NODE);
#else
#error "IMU node does not exist"
static struct i2c_dt_spec sensor_imu_dev = {0};
#endif
static uint8_t sensor_imu_dev_reg = 0xFF;

#if DT_NODE_EXISTS(DT_NODELABEL(mag))
#define SENSOR_MAG_EXISTS true
#define SENSOR_MAG_NODE DT_NODELABEL(mag)
static struct i2c_dt_spec sensor_mag_dev = I2C_DT_SPEC_GET(SENSOR_MAG_NODE);
#else
#warning "Magnetometer node does not exist"
static struct i2c_dt_spec sensor_mag_dev = {0};
#endif
static uint8_t sensor_mag_dev_reg = 0xFF;

static float q[4] = {1.0f, 0.0f, 0.0f, 0.0f}; // vector to hold quaternion
static float last_q[4] = {1.0f, 0.0f, 0.0f, 0.0f}; // vector to hold quaternion

static float q3[4] = {SENSOR_QUATERNION_CORRECTION}; // correction quaternion

static int64_t last_data_time;
static int64_t last_info_time;

static float max_gyro_speed_square;
static bool mag_use_oneshot;

static float accel_actual_time;
static float gyro_actual_time;
static float mag_actual_time;

static bool sensor_fusion_init;
static bool sensor_sensor_init;

static bool sensor_sensor_scanning;

static bool main_suspended;

static bool mag_available;
#if MAG_ENABLED
static bool mag_enabled = true; // TODO: toggle from server
#else
static bool mag_enabled = false;
#endif

#if CONFIG_SENSOR_USE_XIOFUSION
static const sensor_fusion_t *sensor_fusion = &sensor_fusion_fusion; // TODO: change from server
int fusion_id = FUSION_FUSION;
#elif CONFIG_SENSOR_USE_NXPSENSORFUSION
static const sensor_fusion_t *sensor_fusion = &sensor_fusion_motionsense; // TODO: change from server
int fusion_id = FUSION_MOTIONSENSE;
#elif CONFIG_SENSOR_USE_VQF
static const sensor_fusion_t *sensor_fusion = &sensor_fusion_vqf; // TODO: change from server
int fusion_id = FUSION_VQF;
#endif

static int sensor_imu_id = -1;
static int sensor_mag_id = -1;
static const sensor_imu_t *sensor_imu = &sensor_imu_none;
static const sensor_mag_t *sensor_mag = &sensor_mag_none;
static bool use_ext_fifo = false;

LOG_MODULE_REGISTER(sensor, LOG_LEVEL_INF);

K_THREAD_DEFINE(main_imu_thread_id, 2048, main_imu_thread, NULL, NULL, NULL, 7, 0, 0);

const char *sensor_get_sensor_imu_name(void)
{
	if (sensor_imu_id < 0)
		return "None";
	return dev_imu_names[sensor_imu_id];
}

const char *sensor_get_sensor_mag_name(void)
{
	if (sensor_mag_id < 0)
		return "None";
	return dev_mag_names[sensor_mag_id];
}

const char *sensor_get_sensor_fusion_name(void)
{
	if (fusion_id < 0)
		return "None";
	return fusion_names[fusion_id];
}

int sensor_init(void)
{
	while (sensor_sensor_scanning)
		k_usleep(1); // already scanning
	if (sensor_sensor_init)
		return 0; // already initialized
	sensor_sensor_scanning = true;

	sensor_scan_read();

#if SENSOR_IMU_EXISTS
	LOG_INF("Scanning bus for IMU");
	int imu_id = sensor_scan_imu(&sensor_imu_dev, &sensor_imu_dev_reg);
#else
	LOG_ERR("IMU node does not exist");
	int imu_id = -1;
#endif
	if (imu_id >= (int)ARRAY_SIZE(dev_imu_names))
		LOG_WRN("Found unknown device");
	else if (imu_id < 0)
		LOG_ERR("No IMU detected");
	else
		LOG_INF("Found %s", dev_imu_names[imu_id]);
	if (imu_id >= 0)
	{
		if (imu_id >= (int)ARRAY_SIZE(sensor_imus) || sensor_imus[imu_id] == NULL || sensor_imus[imu_id] == &sensor_imu_none)
		{
			sensor_imu = &sensor_imu_none;
			sensor_sensor_scanning = false; // done
//			if (sensor_imu_dev.addr < 0xFF) // If for some reason there actually is a valid IMU but we found some unsupported device first
//			{
//				LOG_WRN("IMU not supported");
//				sensor_imu_dev.addr++;
//				sensor_imu_dev_reg = 0xFF;
//				sensor_scan_clear(); // clear the invalid data
//				return sensor_init(); // try again
//			}
			LOG_ERR("IMU not supported");
			set_status(SYS_STATUS_SENSOR_ERROR, true);
			return -1; // an IMU was detected but not supported
		}
		else
		{
			sensor_imu = sensor_imus[imu_id];
		}
	}
	else
	{
		sensor_imu = &sensor_imu_none;
		sensor_sensor_scanning = false; // done
		set_status(SYS_STATUS_SENSOR_ERROR, true);
		return -1; // no IMU detected! something is very wrong
	}

#if SENSOR_MAG_EXISTS
	LOG_INF("Scanning bus for magnetometer");
	int mag_id = sensor_scan_mag(&sensor_mag_dev, &sensor_mag_dev_reg);
	if (mag_id < 0)
	{
		// IMU must support passthrough mode if the magnetometer is connected through the IMU
		int err = sensor_imu->ext_passthrough(&sensor_imu_dev, true);
		if (!err)
		{
			LOG_INF("Scanning bus for magnetometer through IMU passthrough");
			if (sensor_mag_dev.addr > 0x80) // marked as passthrough
			{
				sensor_mag_dev.addr &= 0x7F;
			}
			else
			{
				sensor_mag_dev.addr = 0x00; // reset magnetometer data
				sensor_mag_dev_reg = 0xFF;
			}
			mag_id = sensor_scan_mag(&sensor_mag_dev, &sensor_mag_dev_reg);
			if (mag_id >= 0)
			{
				sensor_mag_dev.addr |= 0x80; // mark as passthrough
				use_ext_fifo = true;
			}
		}
		// sensor_imu->ext_passthrough(&sensor_imu_dev, false);
	}
	else
	{
		use_ext_fifo = false;
	}
#else
	LOG_WRN("Magnetometer node does not exist");
	int mag_id = -1;
#endif
	if (mag_id >= (int)ARRAY_SIZE(dev_mag_names))
		LOG_WRN("Found unknown device");
	else if (mag_id < 0)
		LOG_WRN("No magnetometer detected");
	else
		LOG_INF("Found %s", dev_mag_names[mag_id]);
	if (mag_id >= 0) // if there is no magnetometer we do not care as much
	{
		if (mag_id >= (int)ARRAY_SIZE(sensor_mags) || sensor_mags[mag_id] == NULL || sensor_mags[mag_id] == &sensor_mag_none)
		{
			sensor_mag = &sensor_mag_none; 
			mag_available = false;
//			if (sensor_imu_dev.addr < 0xFF) // If for some reason there actually is a valid magnetometer but we found some unsupported device first
//			{
//				LOG_WRN("Magnetometer not supported");
//				sensor_mag_dev.addr++;
//				sensor_mag_dev_reg = 0xFF;
//				sensor_scan_clear(); // clear the invalid data
//				return sensor_init(); // try again
//			}
			LOG_ERR("Magnetometer not supported");
		}
		else
		{
			sensor_mag = sensor_mags[mag_id];
			mag_available = true;
		}
	}
	else
	{
		sensor_mag = &sensor_mag_none; 
		mag_available = false; // marked as not available
	}
	if (use_ext_fifo)
	{
		int err = mag_ext_setup(sensor_imu, sensor_mag, sensor_mag_dev.addr);
		if (err)
		{
			LOG_ERR("Magnetometer not supported by external interface");
			sensor_mag = &sensor_mag_none;
			mag_available = false;
		}
		else
		{
			sensor_mag = &sensor_mag_ext;
			mag_available = true;
		}
		
	}

	sensor_scan_write();
	connection_update_sensor_ids(imu_id, mag_id);
	sensor_imu_id = imu_id;
	sensor_mag_id = mag_id;

	sensor_sensor_init = true; // successfully initialized
	sensor_sensor_scanning = false; // done
	set_status(SYS_STATUS_SENSOR_ERROR, false); // clear error
	return 0;
}

void sensor_scan_read(void) // TODO: move some of this to sys?
{
	if (retained.imu_addr != 0)
	{
		sensor_imu_dev.addr = retained.imu_addr;
		sensor_imu_dev_reg = retained.imu_reg;
	}
	if (retained.mag_addr != 0)
	{
		sensor_mag_dev.addr = retained.mag_addr;
		sensor_mag_dev_reg = retained.mag_reg;
	}
	LOG_INF("IMU address: 0x%02X, register: 0x%02X", sensor_imu_dev.addr, sensor_imu_dev_reg);
	LOG_INF("Magnetometer address: 0x%02X, register: 0x%02X", sensor_mag_dev.addr, sensor_mag_dev_reg);
}

void sensor_scan_write(void) // TODO: move some of this to sys?
{
	retained.imu_addr = sensor_imu_dev.addr;
	retained.mag_addr = sensor_mag_dev.addr;
	retained.imu_reg = sensor_imu_dev_reg;
	retained.mag_reg = sensor_mag_dev_reg;
	retained_update();
}

void sensor_scan_clear(void) // TODO: move some of this to sys?
{
	retained.imu_addr = 0x00;
	retained.mag_addr = 0x00;
	retained.imu_reg = 0xFF;
	retained.mag_reg = 0xFF;
	retained_update();
}

void sensor_retained_read(void) // TODO: move some of this to sys?
{
	// Read calibration from retained
	sensor_calibration_read();
#if CONFIG_SENSOR_USE_6_SIDE_CALIBRATION
	LOG_INF("Accelerometer matrix:");
	for (int i = 0; i < 3; i++)
		LOG_INF("%.5f %.5f %.5f %.5f", (double)retained.accBAinv[0][i], (double)retained.accBAinv[1][i], (double)retained.accBAinv[2][i], (double)retained.accBAinv[3][i]);
#else
	LOG_INF("Accelerometer bias: %.5f %.5f %.5f", (double)retained.accelBias[0], (double)retained.accelBias[1], (double)retained.accelBias[2]);
#endif
	LOG_INF("Gyroscope bias: %.5f %.5f %.5f", (double)retained.gyroBias[0], (double)retained.gyroBias[1], (double)retained.gyroBias[2]);
	if (mag_available && mag_enabled)
	{
		LOG_INF("Magnetometer bridge offset: %.5f %.5f %.5f", (double)retained.magBias[0], (double)retained.magBias[1], (double)retained.magBias[2]);
		LOG_INF("Magnetometer matrix:");
		for (int i = 0; i < 3; i++)
			LOG_INF("%.5f %.5f %.5f %.5f", (double)retained.magBAinv[0][i], (double)retained.magBAinv[1][i], (double)retained.magBAinv[2][i], (double)retained.magBAinv[3][i]);
	}
	if (retained.fusion_id)
		LOG_INF("Fusion data recovered");
}

void sensor_retained_write(void) // TODO: move to sys?
{
	if (!sensor_fusion_init)
		return;
	memcpy(retained.magBias, sensor_calibration_get_magBias(), sizeof(retained.magBias));
	sensor_fusion->save(retained.fusion_data);
	retained.fusion_id = fusion_id;
	retained_update();
}

void sensor_shutdown(void) // Communicate all imus to shut down
{
	int err = sensor_init(); // try initialization if possible
	if (!err)
		sensor_imu->shutdown(&sensor_imu_dev);
	else
		LOG_ERR("Failed to shutdown sensors");
	if (mag_available)
		sensor_mag->shutdown(&sensor_mag_dev);
}

void sensor_setup_WOM(void)
{
	int err = sensor_init(); // try initialization if possible
	if (!err)
		sensor_imu->setup_WOM(&sensor_imu_dev);
	else
		LOG_ERR("Failed to configure IMU wake up");
}

void sensor_fusion_invalidate(void)
{
	if (sensor_fusion_init)
	{ // clear fusion gyro offset
		float g_off[3] = {0};
		sensor_fusion->set_gyro_bias(g_off);
		sensor_retained_write();
	}
	else
	{ // TODO: always clearing the fusion?
		retained.fusion_id = 0; // Invalidate retained fusion data
		retained_update();
	}
}

int sensor_update_time_ms = 6;

// TODO: get rid of it.. ?
static void set_update_time_ms(int time_ms)
{
	sensor_update_time_ms = time_ms; // TODO: terrible naming
}

int main_imu_init(void)
{
	int err;
	// TODO: on any errors set main_ok false and skip (make functions return nonzero)
	err = sensor_init(); // IMUs discovery
	if (err)
	{
		k_msleep(5);
		LOG_INF("Retrying sensor detection");
		err = sensor_init(); // on POR, the sensor may not be ready yet
		if (err)
			return err;
	}
	sensor_imu->shutdown(&sensor_imu_dev); // TODO: is this needed?
	if (mag_available)
		sensor_mag->shutdown(&sensor_mag_dev); // TODO: is this needed?

	float clock_actual_rate = 0;
#if CONFIG_USE_SENSOR_CLOCK
	set_sensor_clock(true, 32768, &clock_actual_rate); // enable the clock source for IMU if present
#endif
	if (clock_actual_rate != 0)
		LOG_INF("Sensor clock rate: %.2fHz", (double)clock_actual_rate);

	k_usleep(250); // wait for sensor register reset // TODO: is this needed?
	float accel_initial_time = sensor_update_time_ms / 1000.0; // configure with ~200Hz ODR
	float gyro_initial_time = 1.0 / CONFIG_SENSOR_GYRO_ODR; // configure with ~1000Hz ODR
	err = sensor_imu->init(&sensor_imu_dev, clock_actual_rate, accel_initial_time, gyro_initial_time, &accel_actual_time, &gyro_actual_time);
	LOG_INF("Accelerometer initial rate: %.2fHz", 1.0 / (double)accel_actual_time);
	LOG_INF("Gyrometer initial rate: %.2fHz", 1.0 / (double)gyro_actual_time);
	if (err < 0)
		return err;
// 55-66ms to wait, get chip ids, and setup icm (50ms spent waiting for accel and gyro to start)
	if (mag_available && mag_enabled)
	{
		err = sensor_mag->init(&sensor_mag_dev, sensor_update_time_ms / 1000.0, &mag_actual_time); // configure with ~200Hz ODR
		LOG_INF("Magnetometer initial rate: %.2fHz", 1.0 / (double)mag_actual_time);
		if (err < 0)
			return err;
// 0-1ms to setup mmc
	}
	LOG_INF("Initialized sensors");

	// Setup fusion
	sensor_retained_read();
	if (retained.fusion_id == fusion_id) // Check if the retained fusion data is valid and matches the selected fusion
	{ // Load state if the data is valid (fusion was initialized before)
		sensor_fusion->load(retained.fusion_data);
		retained.fusion_id = 0; // Invalidate retained fusion data
		retained_update();
	}
	else
	{
		sensor_fusion->init(gyro_actual_time, accel_initial_time, accel_initial_time); // TODO: using initial time since accel and mag are not polled at the actual rate
	}

	// Calibrate IMU
	if (isnan(sensor_calibration_get_accelBias()[0]))
		sensor_calibrate_imu(sensor_imu, &sensor_imu_dev);
	else
		sensor_calibration_validate();

#if CONFIG_SENSOR_USE_6_SIDE_CALIBRATION 
	// Calibrate 6-side
	if (isnan(sensor_calibration_get_accBAinv()[0][0]))
		sensor_calibrate_6_side(sensor_imu, &sensor_imu_dev);
	else
		sensor_calibration_validate_6_side();
#endif

	// Verfify magnetometer calibration
	sensor_calibration_validate_mag();

	LOG_INF("Using %s", fusion_names[fusion_id]);
	LOG_INF("Initialized fusion");
	sensor_fusion_init = true;
	return 0;
}

enum sensor_sensor_mode {
//	SENSOR_SENSOR_MODE_OFF,
	SENSOR_SENSOR_MODE_LOW_NOISE,
	SENSOR_SENSOR_MODE_LOW_POWER
};

static enum sensor_sensor_mode sensor_mode = SENSOR_SENSOR_MODE_LOW_NOISE;
static enum sensor_sensor_mode last_sensor_mode = SENSOR_SENSOR_MODE_LOW_NOISE;

static bool main_running = false;
static bool main_ok = false;
static bool send_info = false;

static int packet_errors = 0;

void main_imu_thread(void)
{
	main_running = true;
	int err = main_imu_init(); // Initialize IMUs and Fusion
	// TODO: handle imu init error, maybe restart device?
	if (err)
		set_status(SYS_STATUS_SENSOR_ERROR, true); // TODO: only handles general init error
	else
		main_ok = true;
	while (1)
	{
		int64_t time_begin = k_uptime_get();
		if (main_ok)
		{
			// Trigger reconfig on sensor mode change
			bool reconfig = last_sensor_mode != sensor_mode;
			last_sensor_mode = sensor_mode;

			// Reading IMUs will take between 2.5ms (~7 samples, low noise) - 7ms (~33 samples, low power)
			// Magneto sample will take ~400us
			// Fusing data will take between 100us (~7 samples, low noise) - 500us (~33 samples, low power) for xiofusion
			// TODO: on any errors set main_ok false and skip (make functions return nonzero)

			// At high speed, use oneshot mode to have synced magnetometer data
			// Call before FIFO and get the data after
			if (mag_available && mag_enabled && mag_use_oneshot)
				sensor_mag->mag_oneshot(&sensor_mag_dev);

			// Read IMU temperature
			float temp = sensor_imu->temp_read(&sensor_imu_dev); // TODO: use as calibration data
			connection_update_sensor_temp(temp);

			// Read gyroscope (FIFO)
			uint8_t *rawData = (uint8_t *)k_malloc(512); // Limit FIFO read to 512 bytes
			uint16_t packets = sensor_imu->fifo_read(&sensor_imu_dev, rawData, 512); // TODO: name this better?
			LOG_DBG("IMU packet count: %u", packets);

			// Read accelerometer
			float raw_a[3];
			sensor_imu->accel_read(&sensor_imu_dev, raw_a);
#if CONFIG_SENSOR_USE_6_SIDE_CALIBRATION
			apply_BAinv(raw_a, sensor_calibration_get_accBAinv());
			float ax = raw_a[0];
			float ay = raw_a[1];
			float az = raw_a[2];
#else
			float *accelBias = sensor_calibration_get_accelBias();
			float ax = raw_a[0] - accelBias[0];
			float ay = raw_a[1] - accelBias[1];
			float az = raw_a[2] - accelBias[2];
#endif
			float a[] = {SENSOR_ACCELEROMETER_AXES_ALIGNMENT};

			// Read magnetometer and process magneto
			float mx = 0, my = 0, mz = 0;
			if (mag_available && mag_enabled && sensor_mode == SENSOR_SENSOR_MODE_LOW_NOISE)
			{
				float m[3];
				sensor_mag->mag_read(&sensor_mag_dev, m);
				float *magBias = sensor_calibration_get_magBias();
				for (int i = 0; i < 3; i++)
					m[i] -= magBias[i];
				sensor_sample_mag(a, m); // 400us
				apply_BAinv(m, sensor_calibration_get_magBAinv());
				mx = m[0];
				my = m[1];
				mz = m[2];
			}
			float m[] = {SENSOR_MAGNETOMETER_AXES_ALIGNMENT};

			if (reconfig) // TODO: get rid of reconfig?
			{
				switch (sensor_mode)
				{
				case SENSOR_SENSOR_MODE_LOW_NOISE:
					set_update_time_ms(6);
					LOG_INF("Switching sensors to low noise");
					break;
				case SENSOR_SENSOR_MODE_LOW_POWER:
					set_update_time_ms(33);
					LOG_INF("Switching sensors to low power");
					if (mag_available && mag_enabled)
						sensor_mag->update_odr(&sensor_mag_dev, INFINITY, &mag_actual_time); // standby/oneshot
					break;
				};
			}

			// Fuse all data
			float g[3] = {0};
			float z[3] = {0};
			max_gyro_speed_square = 0;
			int processed_packets = 0;
			float *gyroBias = sensor_calibration_get_gyroBias();
			for (uint16_t i = 0; i < packets; i++) // TODO: fifo_process_ext is available, need to implement it
			{
				float raw_g[3];
				if (sensor_imu->fifo_process(i, rawData, raw_g))
					continue; // skip on error
				// transform and convert to float values
				float gx = raw_g[0] - gyroBias[0]; //gres
				float gy = raw_g[1] - gyroBias[1]; //gres
				float gz = raw_g[2] - gyroBias[2]; //gres
				float g_aligned[] = {SENSOR_GYROSCOPE_AXES_ALIGNMENT};
				memcpy(g, g_aligned, sizeof(g));

				// Process fusion
				sensor_fusion->update_gyro(g, gyro_actual_time);
//				sensor_fusion->update(g, a, m, gyro_actual_time);

				if (mag_available && mag_enabled)
				{
					// Get fusion's corrected gyro data (or get gyro bias from fusion) and use it here
					float g_off[3] = {};
					sensor_fusion->get_gyro_bias(g_off);
					for (int i = 0; i < 3; i++)
						g_off[i] = g[i] - g_off[i];

					// Get the highest gyro speed
					float gyro_speed_square = g_off[0] * g_off[0] + g_off[1] * g_off[1] + g_off[2] * g_off[2];
					if (gyro_speed_square > max_gyro_speed_square)
						max_gyro_speed_square = gyro_speed_square;
				}
				processed_packets++;
			}
			sensor_fusion->update(z, a, m, sensor_update_time_ms / 1000.0); // TODO: use actual time?

			// Free the FIFO buffer
			k_free(rawData);

			// Check packet processing
			if (processed_packets == 0)
			{
				LOG_WRN("No packets processed");
				if (++packet_errors == 10)
				{
					LOG_ERR("Packet error threshold exceeded");
					set_status(SYS_STATUS_SENSOR_ERROR, true); // kind of redundant
					sensor_retained_write(); // keep the fusion state
					sys_request_system_reboot();
				}
			}
			else if (processed_packets < packets)
			{
				LOG_WRN("Only %u/%u packets processed", processed_packets, packets);
			}
			else
			{
				packet_errors = 0;
			}

			// Update fusion gyro sanity?
			sensor_fusion->update_gyro_sanity(g, m);

			// Get updated linear acceleration and quaternion from fusion
			float lin_a[3] = {0};
			sensor_fusion->get_lin_a(lin_a);
			sensor_fusion->get_quat(q);
			q_normalize(q, q); // safe to use self as output

			// Check the IMU gyroscope
			if (sensor_fusion->get_gyro_sanity() == 0 ? q_epsilon(q, last_q, 0.005) : q_epsilon(q, last_q, 0.05)) // Probably okay to use the constantly updating last_q
			{
				int64_t imu_timeout = CLAMP(last_data_time, 1 * 1000, 15 * 1000); // Ramp timeout from last_data_time
				if (k_uptime_get() - last_data_time > imu_timeout) // No motion in last 1s - 10s
				{
					last_data_time = INT64_MAX; // only try to suspend once
					LOG_INF("No motion from sensors in %llds", imu_timeout/1000);
#if CONFIG_USE_IMU_WAKE_UP
					sys_request_WOM(); // TODO: should queue shutdown and suspend itself instead
//					main_imu_suspend(); // TODO: auto suspend, the device should configure WOM ASAP but it does not
#endif
				}
				else if (sensor_mode == SENSOR_SENSOR_MODE_LOW_NOISE && k_uptime_get() - last_data_time > 500) // No motion in last 500ms
				{
					LOG_INF("No motion from sensors in 500ms");
					sensor_mode = SENSOR_SENSOR_MODE_LOW_POWER;
				}
			}
			else
			{
				last_data_time = k_uptime_get();
				sensor_mode = SENSOR_SENSOR_MODE_LOW_NOISE;
			}

			// Update magnetometer mode
			if (mag_available && mag_enabled && sensor_mode == SENSOR_SENSOR_MODE_LOW_NOISE)
			{
				float gyro_speed = sqrtf(max_gyro_speed_square);
				float mag_target_time = 1.0f / (4 * gyro_speed); // target mag ODR for ~0.25 deg error
				if (mag_target_time < 0.005f) // cap at 0.005 (200hz), above this the sensor will use oneshot mode instead
				{
					mag_target_time = 0.005;
					int err = sensor_mag->update_odr(&sensor_mag_dev, INFINITY, &mag_actual_time);
					if (!err)
						LOG_DBG("Switching magnetometer to oneshot");
					mag_use_oneshot = true;
				}
				if (mag_target_time >= 0.005f || mag_actual_time != INFINITY) // under 200Hz or magnetometer did not have a oneshot mode
				{
					int err = sensor_mag->update_odr(&sensor_mag_dev, mag_target_time, &mag_actual_time);
					if (!err)
						LOG_DBG("Switching magnetometer ODR to %.2fHz", 1.0 / (double)mag_actual_time);
					mag_use_oneshot = false;
				}
			}

			// Check if last status is outdated
			if (!send_info && (k_uptime_get() - last_info_time > 100))
			{
				send_info = true;
				last_info_time = k_uptime_get();
			}

			// Send packet with new orientation
			if (!q_epsilon(q, last_q, 0.001))
			{
				bool send_precise_quat = q_epsilon(q, last_q, 0.005);
				memcpy(last_q, q, sizeof(q));
				float q_offset[4];
				q_multiply(q, q3, q_offset);
				v_rotate(lin_a, q3, lin_a);
				connection_update_sensor_data(q_offset, lin_a);
				if (send_info && !send_precise_quat) // prioritize quat precision
				{
					connection_write_packet_2();
					send_info = false;
				}
				else
				{
					connection_write_packet_1();
				}
			}
			else if (send_info)
			{
				connection_write_packet_0();
				send_info = false;
			}

			// Handle magnetometer calibration or bridge offset calibration
			if (mag_available && mag_enabled && last_sensor_mode == SENSOR_SENSOR_MODE_LOW_POWER && sensor_mode == SENSOR_SENSOR_MODE_LOW_POWER)
			{
				if (sensor_calibration_get_mag_progress() == 0b111111) // Save magnetometer calibration while idling
				{
					sensor_calibrate_mag();
				}
				else // only enough time to do one of the two
				{
					sensor_mag->temp_read(&sensor_mag_dev, sensor_calibration_get_magBias()); // for some applicable magnetometer, calibrates bridge offsets
					sensor_retained_write();
				}
			}
		}
		main_running = false;
//		k_sleep(K_FOREVER);
		int64_t time_delta = k_uptime_get() - time_begin;
//		led_clock_offset += time_delta;
		if (time_delta > sensor_update_time_ms)
			k_yield();
		else
			k_msleep(sensor_update_time_ms - time_delta);
		main_running = true;
	}
}

void wait_for_threads(void) // TODO: add timeout
{
	while (main_running)
		k_usleep(1); // bane of my existence. don't use k_yield()!!!!!!
}

void main_imu_suspend(void) // TODO: add timeout
{
	main_suspended = true;
	while (sensor_sensor_scanning)
		k_usleep(1); // try not to interrupt scanning
	while (main_running) // TODO: change to detect if i2c is busy
		k_usleep(1); // try not to interrupt anything actually
	k_thread_suspend(main_imu_thread_id);
	main_running = false; // TODO: redundant
	LOG_INF("Suspended sensor thread");
}

void main_imu_wakeup(void)
{
	if (!main_suspended) // don't wake up if pending suspension
		k_wakeup(main_imu_thread_id);
}