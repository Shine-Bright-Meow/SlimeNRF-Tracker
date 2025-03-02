#include <math.h>

#include <zephyr/logging/log.h>
#include <zephyr/drivers/i2c.h>
#include <hal/nrf_gpio.h>

#include "LSM6DSO.h"
#include "LSM6DSV.h" // Common functions

#define PACKET_SIZE 7

static uint8_t last_accel_mode = 0xff;
static uint8_t last_gyro_mode = 0xff;
static uint8_t last_accel_odr = 0xff;
static uint8_t last_gyro_odr = 0xff;

static uint8_t ext_addr = 0xff;
static uint8_t ext_reg = 0xff;
static bool use_ext_fifo = false;

LOG_MODULE_REGISTER(LSM6DSO, LOG_LEVEL_DBG);

int lsm6dso_init(const struct i2c_dt_spec *dev_i2c, float clock_rate, float accel_time, float gyro_time, float *accel_actual_time, float *gyro_actual_time)
{
	int err = i2c_reg_write_byte_dt(dev_i2c, LSM6DSO_CTRL3, 0x44); // freeze register until done reading, increment register address during multi-byte access (BDU, IF_INC)
//	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSO_CTRL8, 0x00); // Old mode (allows 16g) | XL_FS_MODE = 0
	if (err)
		LOG_ERR("I2C error");
	last_accel_odr = 0xff; // reset last odr
	last_gyro_odr = 0xff; // reset last odr
	err |= lsm6dso_update_odr(dev_i2c, accel_time, gyro_time, accel_actual_time, gyro_actual_time);
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSO_FIFO_CTRL4, 0x06); // enable Continuous mode
	if (err)
		LOG_ERR("I2C error");
	if (use_ext_fifo)
		err |= lsm_ext_init(dev_i2c, ext_addr, ext_reg);
	return (err < 0 ? err : 0);
}

void lsm6dso_shutdown(const struct i2c_dt_spec *dev_i2c)
{
	last_accel_odr = 0xff; // reset last odr
	last_gyro_odr = 0xff; // reset last odr
	int err = i2c_reg_write_byte_dt(dev_i2c, LSM6DSO_CTRL3, 0x01); // SW_RESET
	if (err)
		LOG_ERR("I2C error");
}

int lsm6dso_update_odr(const struct i2c_dt_spec *dev_i2c, float accel_time, float gyro_time, float *accel_actual_time, float *gyro_actual_time)
{
	int ODR;
	uint8_t OP_MODE_XL;
	uint8_t OP_MODE_G;
	uint8_t ODR_XL;
	uint8_t ODR_G;
	uint8_t GYRO_SLEEP = DSO_OP_MODE_G_AWAKE;

	// Calculate accel
	if (accel_time <= 0 || accel_time == INFINITY) // off, standby interpreted as off
	{
		// set High perf mode and off odr on XL
		OP_MODE_XL = DSO_OP_MODE_XL_HP;
		ODR_XL = DSO_ODR_OFF;
		ODR = 0;
	}
	else
	{
		// set High perf mode and select odr on XL
		OP_MODE_XL = DSO_OP_MODE_XL_HP;
		ODR = 1 / accel_time;
	}

	if (ODR == 0)
	{
		accel_time = 0; // off
		ODR_XL = DSO_ODR_OFF;
	}
	else if (accel_time < 0.3f / 1000) // in this case it seems better to compare accel_time
	{
		ODR_XL = DSO_ODR_6_66kHz; // TODO: this is absolutely awful
		accel_time = 0.15 / 1000;
	}
	else if (accel_time < 0.6f / 1000)
	{
		ODR_XL = DSO_ODR_3_33kHz;
		accel_time = 0.3 / 1000;
	}
	else if (accel_time < 1.2f / 1000)
	{
		ODR_XL = DSO_ODR_1_66kHz;
		accel_time = 0.6 / 1000;
	}
	else if (accel_time < 2.4f / 1000)
	{
		ODR_XL = DSO_ODR_833Hz;
		accel_time = 1.2 / 1000;
	}
	else if (accel_time < 4.8f / 1000)
	{
		ODR_XL = DSO_ODR_416Hz;
		accel_time = 2.4 / 1000;
	}
	else if (accel_time < 9.6f / 1000)
	{
		ODR_XL = DSO_ODR_208Hz;
		accel_time = 4.8 / 1000;
	}
	else if (accel_time < 19.2f / 1000)
	{
		ODR_XL = DSO_ODR_104Hz;
		accel_time = 9.6 / 1000;
	}
	else if (accel_time < 38.4f / 1000)
	{
		ODR_XL = DSO_ODR_52Hz;
		accel_time = 19.2 / 1000;
	}
	else if (ODR > 12.5)
	{
		ODR_XL = DSO_ODR_26Hz;
		accel_time = 38.4 / 1000;
	}
	else
	{
		ODR_XL = DSO_ODR_12_5Hz;
		accel_time = 1.0 / 12.5; // 13Hz -> 76.8 / 1000
	}

	// Calculate gyro
	if (gyro_time <= 0) // off
	{
		OP_MODE_G = DSO_OP_MODE_G_HP;
		ODR_G = DSO_ODR_OFF;
		ODR = 0;
	}
	else if (gyro_time == INFINITY) // sleep
	{
		OP_MODE_G = DSO_OP_MODE_G_NP;
		GYRO_SLEEP = DSO_OP_MODE_G_SLEEP;
		ODR_G = last_gyro_odr; // using last ODR
		ODR = 0;
	}
	else
	{
		OP_MODE_G = DSO_OP_MODE_G_HP;
		ODR_G = 0; // the compiler complains unless I do this
		ODR = 1 / gyro_time;
	}

	if (ODR == 0)
	{
		gyro_time = 0; // off
		ODR_G = DSO_ODR_OFF;
	}
	else if (gyro_time < 0.3f / 1000) // in this case it seems better to compare gyro_time
	{
		ODR_G = DSO_ODR_6_66kHz; // TODO: this is absolutely awful
		gyro_time = 1.0 / 6660;
	}
	else if (gyro_time < 0.6f / 1000)
	{
		ODR_G = DSO_ODR_3_33kHz;
		gyro_time = 0.3 / 1000;
	}
	else if (gyro_time < 1.2f / 1000)
	{
		ODR_G = DSO_ODR_1_66kHz;
		gyro_time = 0.6 / 1000;
	}
	else if (gyro_time < 2.4f / 1000)
	{
		ODR_G = DSO_ODR_833Hz;
		gyro_time = 1.2 / 1000;
	}
	else if (gyro_time < 4.8f / 1000)
	{
		ODR_G = DSO_ODR_416Hz;
		gyro_time = 2.4 / 1000;
	}
	else if (gyro_time < 9.6f / 1000)
	{
		ODR_G = DSO_ODR_208Hz;
		gyro_time = 4.8 / 1000;
	}
	else if (gyro_time < 19.2f / 1000)
	{
		ODR_G = DSO_ODR_104Hz;
		gyro_time = 9.6 / 1000;
	}
	else if (gyro_time < 38.4f / 1000)
	{
		ODR_G = DSO_ODR_52Hz;
		gyro_time = 19.2 / 1000;
	}
	else if (ODR > 12.5)
	{
		ODR_G = DSO_ODR_26Hz;
		gyro_time = 38.4 / 1000;
	}
	else
	{
		ODR_G = DSO_ODR_12_5Hz;
		gyro_time = 1.0 / 12.5; // 13Hz -> 76.8 / 1000
	}

	if (last_accel_mode == OP_MODE_XL && last_gyro_mode == OP_MODE_G && last_accel_odr == ODR_XL && last_gyro_odr == ODR_G) // if both were already configured
		return 1;

	last_accel_mode = OP_MODE_XL;
	last_gyro_mode = OP_MODE_G;
	last_accel_odr = ODR_XL;
	last_gyro_odr = ODR_G;

	int err = i2c_reg_write_byte_dt(dev_i2c, LSM6DSO_CTRL1, ODR_XL | DSO_FS_XL_16G); // set accel ODR and FS
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSO_CTRL6, OP_MODE_XL); // set accelerator perf mode

	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSO_CTRL2, ODR_G | DSO_FS_G_2000DPS); // set gyro ODR and mode
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSO_CTRL7, OP_MODE_G); // set gyroscope perf mode
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSO_CTRL4, GYRO_SLEEP); // set gyroscope awake/sleep mode

	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSO_FIFO_CTRL3, (ODR_XL >> 4) | ODR_G); // set accel and gyro batch rate
	if (err)
		LOG_ERR("I2C error");

	*accel_actual_time = accel_time;
	*gyro_actual_time = gyro_time;

	return 0;
}

uint16_t lsm6dso_fifo_read(const struct i2c_dt_spec *dev_i2c, uint8_t *data, uint16_t len)
{
	int err = 0;
	uint16_t total = 0;
	uint16_t count = UINT16_MAX;
	while (count > 0 && len >= PACKET_SIZE)
	{
		uint8_t rawCount[2];
		err |= i2c_burst_read_dt(dev_i2c, LSM6DSO_FIFO_STATUS1, &rawCount[0], 2);
		count = (uint16_t)((rawCount[1] & 3) << 8 | rawCount[0]); // Turn the 16 bits into a unsigned 16-bit value // TODO: might be 3 bits not 2
		uint16_t limit = len / PACKET_SIZE;
		if (count > limit)
			count = limit;
		for (int i = 0; i < count; i++)
			err |= i2c_burst_read_dt(dev_i2c, LSM6DSO_FIFO_DATA_OUT_TAG, &data[i * PACKET_SIZE], PACKET_SIZE);
		if (err)
			LOG_ERR("I2C error");
		data += count * PACKET_SIZE;
		len -= count * PACKET_SIZE;
		total += count;
	}
	return total;
}

uint8_t lsm6dso_setup_WOM(const struct i2c_dt_spec *dev_i2c)
{ // TODO: should be off by the time WOM will be setup
//	i2c_reg_write_byte_dt(dev_i2c, LSM6DSO_CTRL1, ODR_OFF); // set accel off
//	i2c_reg_write_byte_dt(dev_i2c, LSM6DSO_CTRL2, ODR_OFF); // set gyro off

	int err = i2c_reg_write_byte_dt(dev_i2c, LSM6DSO_CTRL1, DSO_ODR_208Hz | DSO_FS_XL_8G); // set accel ODR and FS
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSO_CTRL6, DSO_OP_MODE_XL_NP); // set accel perf mode
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSO_CTRL5, 0x80); // enable accel ULP // TODO: for LSM6DSR/ISM330DHCX this bit may be required to be 0
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSO_CTRL8, 0xF4); // set HPCF_XL to the lowest bandwidth, enable HP_REF_MODE (set HP_REF_MODE_XL, HP_SLOPE_XL_EN, HPCF_XL nonzero)
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSO_TAP_CFG0, 0x10); // set SLOPE_FDS
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSO_WAKE_UP_THS, 0x01); // set threshold, 1 * 31.25 mg is ~31.25 mg
	k_msleep(12); // need to wait for accel to settle

	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSO_TAP_CFG2, 0x80); // enable interrupts
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSO_MD1_CFG, 0x20); // route wake-up to INT1
	if (err)
		LOG_ERR("I2C error");
	return NRF_GPIO_PIN_NOPULL << 4 | NRF_GPIO_PIN_SENSE_HIGH; // active high
}

int lsm6dso_ext_setup(uint8_t addr, uint8_t reg)
{
	ext_addr = addr;
	ext_reg = reg;
	if (addr != 0xff && addr != 0xff)
	{
		use_ext_fifo = true;
		return 0;
	}
	else
	{
		use_ext_fifo = false;
		return 1;
	}
}

const sensor_imu_t sensor_imu_lsm6dso = {
	*lsm6dso_init,
	*lsm6dso_shutdown,

	*lsm6dso_update_odr,

	*lsm6dso_fifo_read,
	*lsm_fifo_process,
	*lsm_accel_read,
	*lsm_gyro_read,
	*lsm_temp_read,

	*lsm6dso_setup_WOM,
	
	*lsm6dso_ext_setup,
	*lsm_fifo_process_ext,
	*lsm_ext_read,
	*lsm_ext_passthrough
};
