/*
 * Copyright (c) 2018-2019 Peter Bigot Consulting, LLC
 * Copyright (c) 2019-2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/dt-bindings/adc/nrf-saadc.h>
#ifdef CONFIG_ADC_NRFX_SAADC
#include <hal/nrf_saadc.h>
#endif
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm13xx_charger.h>
#include <zephyr/logging/log.h>

#include "battery.h"

LOG_MODULE_REGISTER(BATTERY, CONFIG_ADC_LOG_LEVEL);

/* 
 * FIX 1: Map the node label to 'npm1300_charger' as defined in your overlay file.
 * The DT_NODE_HAS_STATUS macro checks if this node is defined and set to "okay".
 */
#define PMIC_CHARGER DT_NODELABEL(npm1300_charger)
#define USE_PMIC_CHARGER DT_NODE_HAS_STATUS(PMIC_CHARGER, okay)

#if USE_PMIC_CHARGER
static const struct device *const charger = DEVICE_DT_GET(PMIC_CHARGER);
static bool battery_ok;
#else

#define VBATT DT_PATH(battery_divider)
#define ZEPHYR_USER DT_PATH(zephyr_user)

struct io_channel_config {
	uint8_t channel;
};

struct divider_config {
	struct io_channel_config io_channel;
	struct gpio_dt_spec power_gpios;
	uint32_t output_ohm;
	uint32_t full_ohm;
};

/* FIX 2: Entirely isolate the ADC structs so they do not evaluate when the PMIC is present */
static const struct divider_config divider_config = {
#if DT_NODE_HAS_STATUS(VBATT, okay)
	.io_channel = {
		DT_IO_CHANNELS_INPUT(VBATT),
	},
	.power_gpios = GPIO_DT_SPEC_GET_OR(VBATT, power_gpios, {}),
	.output_ohm = DT_PROP(VBATT, output_ohms),
	.full_ohm = DT_PROP(VBATT, full_ohms),
#else
#error "Battery divider node does not exist"
	.io_channel = {
		DT_IO_CHANNELS_INPUT(ZEPHYR_USER),
	},
#endif
};

struct divider_data {
	const struct device* adc;
	struct adc_channel_cfg adc_cfg;
	struct adc_sequence adc_seq;
	int16_t raw;
};

static struct divider_data divider_data = {
#if DT_NODE_HAS_STATUS(VBATT, okay)
	.adc = DEVICE_DT_GET(DT_IO_CHANNELS_CTLR(VBATT)),
#else
#error "Battery divider node does not exist"
	.adc = DEVICE_DT_GET(DT_IO_CHANNELS_CTLR(ZEPHYR_USER)),
#endif
};

static bool battery_ok;
#endif

#if !USE_PMIC_CHARGER
#ifdef CONFIG_ADC_NRFX_SAADC
struct battery_adc_gain {
	enum adc_gain gain;
	uint8_t numerator;
	uint8_t denominator;
};

static int battery_select_gain(float max_adc_voltage, uint16_t reference_mv,
			       enum adc_gain *gain)
{
	static const struct battery_adc_gain gains[] = {
#if NRF_SAADC_HAS_GAIN_1_6
		{ ADC_GAIN_1_6, 1, 6 },
#endif
#if NRF_SAADC_HAS_GAIN_1_5
		{ ADC_GAIN_1_5, 1, 5 },
#endif
#if NRF_SAADC_HAS_GAIN_1_4
		{ ADC_GAIN_1_4, 1, 4 },
#endif
#if NRF_SAADC_HAS_GAIN_1_3
		{ ADC_GAIN_1_3, 1, 3 },
#endif
#if NRF_SAADC_HAS_GAIN_1_2
		{ ADC_GAIN_1_2, 1, 2 },
#endif
		{ ADC_GAIN_1, 1, 1 },
	};

	for (size_t i = ARRAY_SIZE(gains); i > 0; --i) {
		const struct battery_adc_gain *candidate = &gains[i - 1];
		if (max_adc_voltage * 1000.0f * candidate->numerator <=
		    (float)reference_mv * candidate->denominator) {
			*gain = candidate->gain;
			return 0;
		}
	}
	return -ERANGE;
}
#endif

static int divider_setup(void) {
	const struct divider_config* cfg = &divider_config;
	const struct io_channel_config* iocp = &cfg->io_channel;
	const struct gpio_dt_spec* gcp = &cfg->power_gpios;
	struct divider_data* ddp = &divider_data;
	struct adc_sequence* asp = &ddp->adc_seq;
	struct adc_channel_cfg* accp = &ddp->adc_cfg;
	int rc;

	if (!device_is_ready(ddp->adc)) {
		LOG_ERR("ADC device is not ready %s", ddp->adc->name);
		return -ENOENT;
	}

	if (gcp->port) {
		if (!device_is_ready(gcp->port)) {
			LOG_ERR("%s: device not ready", gcp->port->name);
			return -ENOENT;
		}
		rc = gpio_pin_configure_dt(gcp, GPIO_OUTPUT_INACTIVE);
		if (rc != 0) {
			LOG_ERR("Failed to control feed %s.%u: %d", gcp->port->name, gcp->pin, rc);
			return rc;
		}
	}

	*asp = (struct adc_sequence){
		.channels = BIT(0),
		.buffer = &ddp->raw,
		.buffer_size = sizeof(ddp->raw),
		.oversampling = 7,
		.calibrate = true,
	};

#ifdef CONFIG_ADC_NRFX_SAADC
	enum adc_gain battery_adc_gain;
	float max_adc_voltage = cfg->output_ohm != 0 ? 5.0f * cfg->output_ohm / cfg->full_ohm : 3.6f;
	uint16_t reference_mv = adc_ref_internal(ddp->adc);

	rc = battery_select_gain(max_adc_voltage, reference_mv, &battery_adc_gain);
	if (rc != 0) {
		return rc;
	}

	*accp = (struct adc_channel_cfg){
		.channel_id = 0,
		.gain = battery_adc_gain,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 3),
	};

	if (cfg->output_ohm != 0) {
		if (iocp->channel == 12) {
			accp->input_positive = NRF_SAADC_VDDHDIV5;
		} else {
			accp->input_positive = iocp->channel;
		}
	} else {
		accp->input_positive = NRF_SAADC_VDD;
	}

	if (iocp->channel == 12) {
		asp->oversampling = 2;
		accp->acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 10);
	}
	asp->resolution = 14;
#else
#error Unsupported ADC
#endif

	rc = adc_channel_setup(ddp->adc, accp);
	return rc;
}
#endif

static int battery_setup(void) {
#if USE_PMIC_CHARGER
	battery_ok = device_is_ready(charger);
	if (!battery_ok) {
		LOG_ERR("nPM1300 charger is not ready");
		return -ENODEV;
	}
	LOG_INF("Battery setup: nPM1300 charger ready");
	return 0;
#else
	int rc = divider_setup();
	battery_ok = (rc == 0);
	return rc;
#endif
}

SYS_INIT(battery_setup, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

int battery_measure_enable(bool enable) {
#if USE_PMIC_CHARGER
	ARG_UNUSED(enable);
	return battery_ok ? 0 : -ENODEV;
#else
	int rc = -ENOENT;
	if (battery_ok) {
		const struct gpio_dt_spec* gcp = &divider_config.power_gpios;
		rc = 0;
		if (gcp->port) {
			rc = gpio_pin_set_dt(gcp, enable);
		}
	}
	return rc;
#endif
}

int battery_sample(void) {
#if USE_PMIC_CHARGER
	if (!battery_ok) {
		return -ENODEV;
	}

	int rc = sensor_sample_fetch(charger);
	if (rc != 0) {
		return rc;
	}

	struct sensor_value voltage;
	/* FIX 3: SENSOR_CHAN_VOLTAGE is the correct channel name mapped to nPM1300-charger */
	rc = sensor_channel_get(charger, SENSOR_CHAN_VOLTAGE, &voltage);
	return rc == 0 ? sensor_value_to_milli(&voltage) : rc;
#else
	int rc = -ENOENT;
	if (battery_ok) {
		struct divider_data* ddp = &divider_data;
		const struct divider_config* dcp = &divider_config;
		struct adc_sequence* sp = &ddp->adc_seq;

		rc = adc_read(ddp->adc, sp);
		sp->calibrate = false;
		if (rc == 0) {
			int32_t adc_uv = ddp->raw;
			rc = adc_raw_to_microvolts(
				adc_ref_internal(ddp->adc),
				ddp->adc_cfg.gain,
				sp->resolution,
				&adc_uv
			);
			if (rc != 0) {
				return rc;
			}
			int64_t battery_uv = adc_uv;
			if (dcp->output_ohm != 0) {
				battery_uv = battery_uv * dcp->full_ohm / dcp->output_ohm;
			}
			rc = battery_uv / 1000;
		}
	}
	return rc;
#endif
}

int battery_charger_state(bool *plugged, bool *charging, bool *charged)
{
#if USE_PMIC_CHARGER
	if (!battery_ok) {
		return -ENODEV;
	}

	struct sensor_value value;
	int rc = sensor_attr_get(charger, SENSOR_CHAN_NPM13XX_CHARGER_VBUS_STATUS,
		SENSOR_ATTR_NPM13XX_CHARGER_VBUS_PRESENT, &value);
	if (rc != 0) {
		return rc;
	}
	*plugged = value.val1 != 0;

	rc = sensor_channel_get(charger, SENSOR_CHAN_NPM13XX_CHARGER_STATUS, &value);
	if (rc != 0) {
		return rc;
	}
	*charged = (value.val1 & BIT(1)) != 0;
	*charging = (value.val1 & (BIT(2) | BIT(3) | BIT(4))) != 0;
	return 0;
#else
	/* Keep old analog fallback states safely unmapped */
	*plugged = false;
	*charging = false;
	*charged = false;
	return 0;
#endif
}
