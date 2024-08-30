#include <math.h>

#include <zephyr/drivers/i2c.h>

#include "BMI270.h"
#include "BMI270_firmware.h"

static const float accel_sensitivity = 16.0f / 32768.0f; // Always 16G
static const float gyro_sensitivity = 2000.0f / 32768.0f; // Always 2000dps

static uint8_t last_accel_odr = 0xff;
static uint8_t last_gyro_odr = 0xff;

int bmi_init(struct i2c_dt_spec dev_i2c, float clock_rate, float accel_time, float gyro_time, float *accel_actual_time, float *gyro_actual_time)
{
	uint8_t status;
	i2c_reg_write_byte_dt(&dev_i2c, BMI270_PWR_CONF, 0x00); // disable adv_power_save
	k_usleep(450);
	i2c_reg_read_byte_dt(&dev_i2c, BMI270_INTERNAL_STATUS, &status);
	if ((status & 0x7) != 0x1) // ASIC is not initialized
	{
		i2c_reg_write_byte_dt(&dev_i2c, BMI270_CMD, 0xB6); // softreset
		k_msleep(2);
		i2c_reg_write_byte_dt(&dev_i2c, BMI270_PWR_CONF, 0x00); // disable adv_power_save
		k_usleep(450);
		bmi_upload_config_file(dev_i2c);
		int retry_count = 0;
		while ((status & 0x7) != 0x1)
		{
			if (retry_count > 100)
				return -1;
			k_msleep(1);
			i2c_reg_read_byte_dt(&dev_i2c, BMI270_INTERNAL_STATUS, &status);
			retry_count++;
		}
	}
	last_accel_odr = 0xff; // reset last odr
	last_gyro_odr = 0xff; // reset last odr
	i2c_reg_write_byte_dt(&dev_i2c, BMI270_ACC_RANGE, RANGE_16G);
	i2c_reg_write_byte_dt(&dev_i2c, BMI270_GYR_RANGE, RANGE_2000);
	int err = bmi_update_odr(dev_i2c, accel_time, gyro_time, accel_actual_time, gyro_actual_time);
	i2c_reg_write_byte_dt(&dev_i2c, BMI270_FIFO_CONFIG_0, 0x01); // stop FIFO when full, do not return sensortime frame
	i2c_reg_write_byte_dt(&dev_i2c, BMI270_FIFO_CONFIG_1, 0x80); // enable gyro data in FIFO, don't store header
	return (err < 0 ? err : 0);
}

void bmi_shutdown(struct i2c_dt_spec dev_i2c) // this does not reset the device, to avoid clearing the config
{
	last_accel_odr = 0xff; // reset last odr
	last_gyro_odr = 0xff; // reset last odr
	i2c_reg_write_byte_dt(&dev_i2c, BMI270_PWR_CTRL, 0x00); // disable all sensors
	i2c_reg_write_byte_dt(&dev_i2c, BMI270_PWR_CONF, 0x01); // enable adv_power_save (suspend)
}

int bmi_update_odr(struct i2c_dt_spec dev_i2c, float accel_time, float gyro_time, float *accel_actual_time, float *gyro_actual_time)
{
	int ODR;
	uint8_t acc_odr;
	uint8_t gyr_odr;

	// Calculate accel
	if (accel_time <= 0 || accel_time == INFINITY) // off, standby interpreted as off
		ODR = 0;
	else
		ODR = 1 / accel_time;

	if (ODR == 0)
	{
		acc_odr = 0; // off
		accel_time = 0; // off
	}
	else if (ODR > 800) // TODO: this is absolutely awful
	{
		acc_odr = ODR_1k6;
		accel_time = 1.0 / 1600;
	}
	else if (ODR > 400)
	{
		acc_odr = ODR_800;
		accel_time = 1.0 / 800;
	}
	else if (ODR > 200)
	{
		acc_odr = ODR_400;
		accel_time = 1.0 / 400;
	}
	else if (ODR > 100)
	{
		acc_odr = ODR_200;
		accel_time = 1.0 / 200;
	}
	else if (ODR > 50)
	{
		acc_odr = ODR_100;
		accel_time = 1.0 / 100;
	}
	else if (ODR > 25)
	{
		acc_odr = ODR_50;
		accel_time = 1.0 / 50;
	}
	else if (ODR > 12.5)
	{
		acc_odr = ODR_25;
		accel_time = 1.0 / 25;
	}
	else if (ODR > 6.25)
	{
		acc_odr = ODR_12p5;
		accel_time = 2.0 / 25;
	}
	else if (ODR > 3.125)
	{
		acc_odr = ODR_6p25;
		accel_time = 4.0 / 25;
	}
	else if (ODR > 1.5625)
	{
		acc_odr = ODR_3p1;
		accel_time = 8.0 / 25;
	}
	else if (ODR > 0.78125)
	{
		acc_odr = ODR_1p5;
		accel_time = 16.0 / 25;
	}
	else
	{
		acc_odr = ODR_0p78;
		accel_time = 32.0 / 25;
	}

	// Calculate gyro
	if (gyro_time <= 0 || gyro_time == INFINITY) // off, standby interpreted as off
		ODR = 0;
	else
		ODR = 1 / accel_time;

	if (ODR == 0)
	{
		gyr_odr = 0; // off
		gyro_time = 0; // off
	}
	else if (ODR > 1600) // TODO: this is absolutely awful
	{
		gyr_odr = ODR_3k2;
		gyro_time = 1.0 / 3200;
	}
	else if (ODR > 800)
	{
		gyr_odr = ODR_1k6;
		gyro_time = 1.0 / 1600;
	}
	else if (ODR > 400)
	{
		gyr_odr = ODR_800;
		gyro_time = 1.0 / 800;
	}
	else if (ODR > 200)
	{
		gyr_odr = ODR_400;
		gyro_time = 1.0 / 400;
	}
	else if (ODR > 100)
	{
		gyr_odr = ODR_200;
		gyro_time = 1.0 / 200;
	}
	else if (ODR > 50)
	{
		gyr_odr = ODR_100;
		gyro_time = 1.0 / 100;
	}
	else if (ODR > 25)
	{
		gyr_odr = ODR_50;
		gyro_time = 1.0 / 50;
	}
	else
	{
		gyr_odr = ODR_25;
		gyro_time = 1.0 / 25;
	}

	if (last_accel_odr == acc_odr && last_gyro_odr == gyr_odr) // if both were already configured
		return 1;

	last_accel_odr = acc_odr;
	last_gyro_odr = gyr_odr;

	if (acc_odr != 0)
		i2c_reg_write_byte_dt(&dev_i2c, BMI270_ACC_CONF, 0xA0 | acc_odr);
	if (gyr_odr != 0)
		i2c_reg_write_byte_dt(&dev_i2c, BMI270_GYR_CONF, 0xE0 | gyr_odr); // set performance opt. noise performance
	
	i2c_reg_write_byte_dt(&dev_i2c, BMI270_PWR_CTRL, 0x08 | (acc_odr != 0 ? 0x04 : 0) | (gyr_odr != 0 ? 0x02 : 0)); // enable temp, set accel and gyro power

	*accel_actual_time = accel_time;
	*gyro_actual_time = gyro_time;

	return 0;
}

uint16_t bmi_fifo_read(struct i2c_dt_spec dev_i2c, uint8_t *data)
{
	uint8_t rawCount[2];
	i2c_burst_read_dt(&dev_i2c, BMI270_FIFO_LENGTH_0, &rawCount[0], 2);
	uint16_t count = (uint16_t)((rawCount[1] & 1) << 8 | rawCount[0]); // Turn the 16 bits into a unsigned 16-bit value
	count += 24; // Add a few read buffer packets, since the FIFO may contain more data than when we begin reading (allowing 4ms of transaction time for 1000hz ODR)
	uint16_t packets = count / 6; // FIFO packet size is 6 bytes for gyro only
	uint16_t offset = 0;
	while (count > 0)
	{
		i2c_burst_read_dt(&dev_i2c, BMI270_FIFO_DATA, &data[offset], count > 64 ? 64 : count); // Read less than 255 at a time (for nRF52832)
		offset += 64;
		count = count > 64 ? count - 64 : 0;
	}
	return packets;
}

int bmi_fifo_process(uint16_t index, uint8_t *data, float g[3])
{
	index *= 6; // Packet size 6 bytes
	if (data[index] == 0x00 && data[index + 1] == 0x80)
		return 1; // Skip overread packets
	for (int i = 0; i < 3; i++) // x, y, z
	{
		g[i] = (int16_t)((((int16_t)data[index + (i * 2) + 1]) << 8) | data[index + (i * 2)]);
		g[i] *= gyro_sensitivity;
	}
	return 0;
}

void bmi_accel_read(struct i2c_dt_spec dev_i2c, float a[3])
{
	uint8_t rawAccel[6];
	i2c_burst_read_dt(&dev_i2c, BMI270_DATA_8, &rawAccel[0], 6);
	for (int i = 0; i < 3; i++) // x, y, z
	{
		a[i] = (int16_t)((((int16_t)rawAccel[(i * 2) + 1]) << 8) | rawAccel[i * 2]);
		a[i] *= accel_sensitivity;
	}
}

void bmi_gyro_read(struct i2c_dt_spec dev_i2c, float g[3])
{
	uint8_t rawGyro[6];
	i2c_burst_read_dt(&dev_i2c, BMI270_DATA_14, &rawGyro[0], 6);
	for (int i = 0; i < 3; i++) // x, y, z
	{
		g[i] = (int16_t)((((int16_t)rawGyro[(i * 2) + 1]) << 8) | rawGyro[i * 2]);
		g[i] *= gyro_sensitivity;
	}
}

float bmi_temp_read(struct i2c_dt_spec dev_i2c)
{
	uint8_t rawTemp[2];
	i2c_burst_read_dt(&dev_i2c, BMI270_TEMPERATURE_0, &rawTemp[0], 2);
	if (rawTemp[0] == 0x00 && rawTemp[1] == 0x80)
		return 23; // TODO: invalid temperature, what to return?
	// 0x0000 -> 23°C
	// The resolution is 1/2^9 K/LSB
	float temp = (int16_t)((((int16_t)rawTemp[1]) << 8) | rawTemp[0]);
	temp /= 512;
	temp += 23;
	return temp;
}

void bmi_setup_WOM(struct i2c_dt_spec dev_i2c)
{
	uint8_t config[4] = {0};
	uint16_t *ptr = (uint16_t *)config;
	ptr[0] = 0x7 << 13 | 0x000; // enable all axes, set detection duration to 0
	ptr[1] = 0x1 << 15 | 0x7 << 11 | 0x28A; // enable any_motion, set out_conf, set threshold (1LSB equals to 0.48mg, 650 * 0.48mg is ~312mg)
	i2c_reg_write_byte_dt(&dev_i2c, BMI270_PWR_CONF, 0x00); // disable adv_power_save
	k_busy_wait(500);
	i2c_reg_write_byte_dt(&dev_i2c, BMI270_ACC_CONF, ODR_200); // disable filters, set accel ODR
	i2c_reg_write_byte_dt(&dev_i2c, BMI270_PWR_CTRL, 0x04); // enable accel
	i2c_reg_write_byte_dt(&dev_i2c, BMI270_FEAT_PAGE, 0x01); // go to page 1
	i2c_burst_write_dt(&dev_i2c, BMI270_ANYMO_1, config, sizeof(config)); // Start write buffer
	i2c_reg_write_byte_dt(&dev_i2c, BMI270_INT1_IO_CTRL, 0x0C); // set INT1 active low, open-drain, output enabled
	i2c_reg_write_byte_dt(&dev_i2c, BMI270_INT1_MAP_FEAT, 0x40); // enable any_motion_out (interrupt)
	i2c_reg_write_byte_dt(&dev_i2c, BMI270_PWR_CONF, 0x01); // enable adv_power_save (suspend)
	k_busy_wait(2000); // wait for sensor to settle
}

// write_config_file function from https://github.com/zephyrproject-rtos/zephyr/blob/main/drivers/sensor/bosch/bmi270/bmi270.c
// saved my ass
int bmi_upload_config_file(struct i2c_dt_spec dev_i2c)
{
	uint16_t count = sizeof(bmi270_config_file) / sizeof(bmi270_config_file[0]);
	uint8_t init_addr[2] = {0};
	i2c_reg_write_byte_dt(&dev_i2c, BMI270_INIT_CTRL, 0x00); // prepare config load
	for (int i = 0; i < count; i += 64)
	{
		init_addr[0] = (i / 2) & 0xF;
		init_addr[1] = (i / 2) >> 4;
		i2c_burst_write_dt(&dev_i2c, BMI270_INIT_ADDR_0, init_addr, 2);
		i2c_burst_write_dt(&dev_i2c, BMI270_INIT_DATA, &bmi270_config_file[i], 64); // 64 works, 128 doesn't? either way it takes forever
	}
	i2c_reg_write_byte_dt(&dev_i2c, BMI270_INIT_CTRL, 0x01); // complete config load
	return 0;
}

const sensor_imu_t sensor_imu_bmi270 = {
	*bmi_init,
	*bmi_shutdown,

	*bmi_update_odr,

	*bmi_fifo_read,
	*bmi_fifo_process,
	*bmi_accel_read,
	*bmi_gyro_read,
	*bmi_temp_read,

	*bmi_setup_WOM
};