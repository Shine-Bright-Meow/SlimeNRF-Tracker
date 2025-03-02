#include <math.h>

#include <zephyr/logging/log.h>
#include <zephyr/drivers/i2c.h>
#include <hal/nrf_gpio.h>

#include "LSM6DSV.h"
#include "sensor/sensor_none.h"

#define PACKET_SIZE 7

static const float accel_sensitivity = 16.0f / 32768.0f; // Always 16G (FS = ±16 g: 0.488 mg/LSB)
static const float gyro_sensitivity = 0.070f; // Always 2000dps (FS = ±2000 dps: 70 mdps/LSB)

static uint8_t last_accel_mode = 0xff;
static uint8_t last_gyro_mode = 0xff;
static uint8_t last_accel_odr = 0xff;
static uint8_t last_gyro_odr = 0xff;

static uint8_t ext_addr = 0xff;
static uint8_t ext_reg = 0xff;
static bool use_ext_fifo = false;

LOG_MODULE_REGISTER(LSM6DSV, LOG_LEVEL_DBG);

int lsm_init(const struct i2c_dt_spec *dev_i2c, float clock_rate, float accel_time, float gyro_time, float *accel_actual_time, float *gyro_actual_time)
{
	int err = i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_CTRL6, FS_G_2000DPS); // set gyro FS
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_CTRL8, FS_XL_16G); // set accel FS
	if (err)
		LOG_ERR("I2C error");
	last_accel_odr = 0xff; // reset last odr
	last_gyro_odr = 0xff; // reset last odr
	err |= lsm_update_odr(dev_i2c, accel_time, gyro_time, accel_actual_time, gyro_actual_time);
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_FIFO_CTRL4, 0x06); // enable Continuous mode
	if (err)
		LOG_ERR("I2C error");
//	if (use_ext_fifo)
//		err |= lsm_ext_init(dev_i2c, ext_addr, ext_reg);
	return (err < 0 ? err : 0);
}

void lsm_shutdown(const struct i2c_dt_spec *dev_i2c)
{
	last_accel_odr = 0xff; // reset last odr
	last_gyro_odr = 0xff; // reset last odr
	int err = i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_CTRL3, 0x01); // SW_RESET
	if (err)
		LOG_ERR("I2C error");
}

int lsm_update_odr(const struct i2c_dt_spec *dev_i2c, float accel_time, float gyro_time, float *accel_actual_time, float *gyro_actual_time)
{
	int ODR;
	uint8_t OP_MODE_XL;
	uint8_t OP_MODE_G;
	uint8_t ODR_XL;
	uint8_t ODR_G;

	// Calculate accel
	if (accel_time <= 0 || accel_time == INFINITY) // off, standby interpreted as off
	{
		OP_MODE_XL = OP_MODE_XL_HP;
		ODR_XL = ODR_OFF;
		ODR = 0;
	}
	else
	{
		OP_MODE_XL = OP_MODE_XL_HP;
		ODR = 1 / accel_time;
	}

	if (ODR == 0)
	{
		accel_time = 0; // off
	}
	else if (ODR > 3840) // TODO: this is absolutely awful
	{
		ODR_XL = ODR_7_68kHz;
		accel_time = 1.0 / 7680;
	}
	else if (ODR > 1920)
	{
		ODR_XL = ODR_3_84kHz;
		accel_time = 1.0 / 3840;
	}
	else if (ODR > 960)
	{
		ODR_XL = ODR_1_92kHz;
		accel_time = 1.0 / 1920;
	}
	else if (ODR > 480)
	{
		ODR_XL = ODR_960Hz;
		accel_time = 1.0 / 960;
	}
	else if (ODR > 240)
	{
		ODR_XL = ODR_480Hz;
		accel_time = 1.0 / 480;
	}
	else if (ODR > 120)
	{
		ODR_XL = ODR_240Hz;
		accel_time = 1.0 / 240;
	}
	else if (ODR > 60)
	{
		ODR_XL = ODR_120Hz;
		accel_time = 1.0 / 120;
	}
	else if (ODR > 30)
	{
		ODR_XL = ODR_60Hz;
		accel_time = 1.0 / 60;
	}
	else if (ODR > 15)
	{
		ODR_XL = ODR_30Hz;
		accel_time = 1.0 / 30;
	}
	else if (ODR > 7.5)
	{
		ODR_XL = ODR_15Hz;
		accel_time = 1.0 / 15;
	}
	else if (ODR > 1.875)
	{
		ODR_XL = ODR_7_5Hz;
		accel_time = 1.0 / 7.5;
	}
	else
	{
		ODR_XL = ODR_1_875Hz;
		accel_time = 1.0 / 1.875;
	}

	// Calculate gyro
	if (gyro_time <= 0) // off
	{
		OP_MODE_G = OP_MODE_G_HP;
		ODR_G = ODR_OFF;
		ODR = 0;
	}
	else if (gyro_time == INFINITY) // sleep
	{
		OP_MODE_G = OP_MODE_G_SLEEP;
		ODR_G = last_gyro_odr; // using last ODR
		ODR = 0;
	}
	else
	{
		OP_MODE_G = OP_MODE_G_HP;
		ODR_G = 0; // the compiler complains unless I do this
		ODR = 1 / gyro_time;
	}

	if (ODR == 0)
	{
		gyro_time = 0; // off
	}
	else if (ODR > 3840) // TODO: this is absolutely awful
	{
		ODR_G = ODR_7_68kHz;
		gyro_time = 1.0 / 7680;
	}
	else if (ODR > 1920)
	{
		ODR_G = ODR_3_84kHz;
		gyro_time = 1.0 / 3840;
	}
	else if (ODR > 960)
	{
		ODR_G = ODR_1_92kHz;
		gyro_time = 1.0 / 1920;
	}
	else if (ODR > 480)
	{
		ODR_G = ODR_960Hz;
		gyro_time = 1.0 / 960;
	}
	else if (ODR > 240)
	{
		ODR_G = ODR_480Hz;
		gyro_time = 1.0 / 480;
	}
	else if (ODR > 120)
	{
		ODR_G = ODR_240Hz;
		gyro_time = 1.0 / 240;
	}
	else if (ODR > 60)
	{
		ODR_G = ODR_120Hz;
		gyro_time = 1.0 / 120;
	}
	else if (ODR > 30)
	{
		ODR_G = ODR_60Hz;
		gyro_time = 1.0 / 60;
	}
	else if (ODR > 15)
	{
		ODR_G = ODR_30Hz;
		gyro_time = 1.0 / 30;
	}
	else if (ODR > 7.5)
	{
		ODR_G = ODR_15Hz;
		gyro_time = 1.0 / 15;
	}
	else if (ODR > 1.875)
	{
		ODR_G = ODR_7_5Hz;
		gyro_time = 1.0 / 7.5;
	}
	else
	{
		ODR_G = ODR_1_875Hz;
		gyro_time = 1.0 / 1.875;
	}

	if (last_accel_mode == OP_MODE_XL && last_gyro_mode == OP_MODE_G && last_accel_odr == ODR_XL && last_gyro_odr == ODR_G) // if both were already configured
		return 1;

	last_accel_mode = OP_MODE_XL;
	last_gyro_mode = OP_MODE_G;
	last_accel_odr = ODR_XL;
	last_gyro_odr = ODR_G;

	int err = i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_CTRL1, OP_MODE_XL << 4 | ODR_XL); // set accel ODR and mode
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_CTRL2, OP_MODE_G << 4 | ODR_G); // set gyro ODR and mode

	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_FIFO_CTRL3, ODR_XL | (ODR_G << 4)); // set accel and gyro batch rate
	if (err)
		LOG_ERR("I2C error");

	*accel_actual_time = accel_time;
	*gyro_actual_time = gyro_time;

	return 0;
}

uint16_t lsm_fifo_read(const struct i2c_dt_spec *dev_i2c, uint8_t *data, uint16_t len)
{
	int err = 0;
	uint16_t total = 0;
	uint16_t count = UINT16_MAX;
	while (count > 0 && len >= PACKET_SIZE)
	{
		uint8_t rawCount[2];
		err |= i2c_burst_read_dt(dev_i2c, LSM6DSV_FIFO_STATUS1, &rawCount[0], 2);
		count = (uint16_t)((rawCount[1] & 3) << 8 | rawCount[0]); // Turn the 16 bits into a unsigned 16-bit value. Only LSB on FIFO_STATUS2 is used, but we mask 2nd bit too // TODO: might be 3 bits not 2
		uint16_t limit = len / PACKET_SIZE;
		if (count > limit)
			count = limit;
		for (int i = 0; i < count; i++)
			err |= i2c_burst_read_dt(dev_i2c, LSM6DSV_FIFO_DATA_OUT_TAG, &data[i * PACKET_SIZE], PACKET_SIZE);
		if (err)
			LOG_ERR("I2C error");
		data += count * PACKET_SIZE;
		len -= count * PACKET_SIZE;
		total += count;
	}
	return total;
}

int lsm_fifo_process(uint16_t index, uint8_t *data, float a[3], float g[3])
{
	index *= PACKET_SIZE;
	if ((data[index] >> 3) == 0x02) // Accelerometer NC (Accelerometer uncompressed data)
	{
		for (int i = 0; i < 3; i++) // x, y, z
		{
			a[i] = (int16_t)((((uint16_t)data[index + 2 + (i * 2)]) << 8) | data[index + 1 + (i * 2)]);
			a[i] *= accel_sensitivity;
		}
		return 0;
	}
	if ((data[index] >> 3) == 0x01) // Gyroscope NC (Gyroscope uncompressed data)
	{
		for (int i = 0; i < 3; i++) // x, y, z
		{
			g[i] = (int16_t)((((uint16_t)data[index + 2 + (i * 2)]) << 8) | data[index + 1 + (i * 2)]);
			g[i] *= gyro_sensitivity;
		}
		return 0;
	}
	// TODO: need to skip invalid data
	return 1;
}

void lsm_accel_read(const struct i2c_dt_spec *dev_i2c, float a[3])
{
	uint8_t rawAccel[6];
	int err = i2c_burst_read_dt(dev_i2c, LSM6DSV_OUTX_L_A, &rawAccel[0], 6);
	if (err)
		LOG_ERR("I2C error");
	for (int i = 0; i < 3; i++) // x, y, z
	{
		a[i] = (int16_t)((((uint16_t)rawAccel[1 + (i * 2)]) << 8) | rawAccel[i * 2]);
		a[i] *= accel_sensitivity;
	}
	// TODO: for ISM330BX, the accelerometer data is in ZYX order
}

void lsm_gyro_read(const struct i2c_dt_spec *dev_i2c, float g[3])
{
	uint8_t rawGyro[6];
	int err = i2c_burst_read_dt(dev_i2c, LSM6DSV_OUTX_L_G, &rawGyro[0], 6);
	if (err)
		LOG_ERR("I2C error");
	for (int i = 0; i < 3; i++) // x, y, z
	{
		g[i] = (int16_t)((((uint16_t)rawGyro[1 + (i * 2)]) << 8) | rawGyro[i * 2]);
		g[i] *= gyro_sensitivity;
	}
}

float lsm_temp_read(const struct i2c_dt_spec *dev_i2c)
{
	uint8_t rawTemp[2];
	int err = i2c_burst_read_dt(dev_i2c, LSM6DSV_OUT_TEMP_L, &rawTemp[0], 2);
	if (err)
		LOG_ERR("I2C error");
	// TSen Temperature sensitivity 256 LSB/°C
	// The output of the temperature sensor is 0 LSB (typ.) at 25°C
	float temp = (int16_t)((((uint16_t)rawTemp[1]) << 8) | rawTemp[0]);
	temp /= 256;
	temp += 25;
	return temp;
}

uint8_t lsm_setup_WOM(const struct i2c_dt_spec *dev_i2c)
{ // TODO: should be off by the time WOM will be setup
//	i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_CTRL1, ODR_OFF); // set accel off
//	i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_CTRL2, ODR_OFF); // set gyro off

	int err = i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_CTRL8, 0xE0 | FS_XL_8G); // set accel FS, set HP_LPF2_XL_BW to lowest bandwidth, enable HP_REF_MODE (set HP_LPF2_XL_BW)
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_CTRL1, OP_MODE_XL_LP1 << 4 | ODR_240Hz); // set accel low power mode 1, set accel ODR (enable accel)
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_CTRL9, 0x50); // enable HP_REF_MODE (set HP_REF_MODE_XL and HP_SLOPE_XL_EN)
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_TAP_CFG0, 0x10); // set SLOPE_FDS
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_WAKE_UP_THS, 0x04); // set threshold, 4 * 7.8125 mg is ~31.25 mg
	k_msleep(11); // need to wait for accel to settle

	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_FUNCTIONS_ENABLE, 0x80); // enable interrupts
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_MD1_CFG, 0x20); // route wake-up to INT1
	if (err)
		LOG_ERR("I2C error");
	return NRF_GPIO_PIN_NOPULL << 4 | NRF_GPIO_PIN_SENSE_HIGH; // active high
}

int lsm_ext_setup(uint8_t addr, uint8_t reg)
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

int lsm_fifo_process_ext(uint16_t index, uint8_t *data, float a[3], float g[3], uint8_t *raw_m)
{
	if (!lsm_fifo_process(index, data, a, g)) // try processing a+g first
		return 0;
	index *= PACKET_SIZE;
	if ((data[index] >> 3) == 0x0E)
	{
		memcpy(raw_m, &data[index + 1], 6);
		return 0;
	}
	// TODO: need to skip invalid data
	return 1;
}

void lsm_ext_read(const struct i2c_dt_spec *dev_i2c, uint8_t *raw_m)
{
	int err = i2c_burst_read_dt(dev_i2c, LSM6DSV_SENSOR_HUB_1, raw_m, 6);
	if (err)
		LOG_ERR("I2C error");
}

int lsm_ext_passthrough(const struct i2c_dt_spec *dev_i2c, bool passthrough)
{
	int err = 0;
	if (passthrough)
	{
		err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_FUNC_CFG_ACCESS, 0x40); // switch to sensor hub registers
		err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_MASTER_CONFIG, 0x10); // passthrough on
		err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_FUNC_CFG_ACCESS, 0x00); // switch to normal registers
	}
	else
	{
		err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_FUNC_CFG_ACCESS, 0x40); // switch to sensor hub registers
		err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_MASTER_CONFIG, 0x00); // passthrough off
		err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_FUNC_CFG_ACCESS, 0x00); // switch to normal registers
	}
	if (err)
		LOG_ERR("I2C error");
	return 0;
}

int lsm_ext_init(const struct i2c_dt_spec *dev_i2c, uint8_t ext_addr, uint8_t ext_reg)
{
	int err = i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_FUNC_CFG_ACCESS, 0x80); // enable sensor hub
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_MASTER_CONFIG, 0x24); // trigger from INT2, MASTER_ON
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_SLV0_ADD, (ext_addr << 1) | 0x01); // set external address, read mode
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_SLV0_SUBADD, ext_reg); // set external register
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_SLV0_CONFIG, 0x08 | 0x06); // enable external sensor fifo and set 6 read operations
	err |= i2c_reg_write_byte_dt(dev_i2c, LSM6DSV_FUNC_CFG_ACCESS, 0x00); // switch to normal registers
	if (err)
		LOG_ERR("I2C error");
	return err;
}

const sensor_imu_t sensor_imu_lsm6dsv = {
	*lsm_init,
	*lsm_shutdown,

	*lsm_update_odr,

	*lsm_fifo_read,
	*lsm_fifo_process,
	*lsm_accel_read,
	*lsm_gyro_read,
	*lsm_temp_read,

	*lsm_setup_WOM,
	
	*imu_none_ext_setup,
	*imu_none_fifo_process_ext,
	*imu_none_ext_read,
	*lsm_ext_passthrough
};
