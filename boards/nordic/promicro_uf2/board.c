/*
 * Copyright (c) 2018 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <hal/nrf_gpio.h>
#include <zephyr/devicetree.h>

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

#if DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, vcc_gpios)
#define VCC_GPIO_PIN DT_GPIO_PIN(ZEPHYR_USER_NODE, vcc_gpios)
#define VCC_GPIO_PORT_NUM DT_PROP(DT_GPIO_CTLR(ZEPHYR_USER_NODE, vcc_gpios), port)
#endif

#if DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, pwr_gpios)
#define PWR_GPIO_PIN DT_GPIO_PIN(ZEPHYR_USER_NODE, pwr_gpios)
#define PWR_GPIO_PORT_NUM DT_PROP(DT_GPIO_CTLR(ZEPHYR_USER_NODE, pwr_gpios), port)
#endif

#if DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, gnd_gpios)
#define GND_GPIO_PIN DT_GPIO_PIN(ZEPHYR_USER_NODE, gnd_gpios)
#define GND_GPIO_PORT_NUM DT_PROP(DT_GPIO_CTLR(ZEPHYR_USER_NODE, gnd_gpios), port)
#endif

static int board_promicro_init(void)
{
#if DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, vcc_gpios)
	nrf_gpio_cfg(NRF_GPIO_PIN_MAP(VCC_GPIO_PORT_NUM, VCC_GPIO_PIN),
				 NRF_GPIO_PIN_DIR_OUTPUT,
				 NRF_GPIO_PIN_INPUT_DISCONNECT,
				 NRF_GPIO_PIN_NOPULL,
				 NRF_GPIO_PIN_D0H1,
				 NRF_GPIO_PIN_NOSENSE);
	nrf_gpio_pin_set(NRF_GPIO_PIN_MAP(VCC_GPIO_PORT_NUM, VCC_GPIO_PIN));
#endif

	nrf_gpio_cfg(NRF_GPIO_PIN_MAP(0, 13),
				 NRF_GPIO_PIN_DIR_INPUT,
				 NRF_GPIO_PIN_INPUT_DISCONNECT,
				 NRF_GPIO_PIN_PULLDOWN,
				 NRF_GPIO_PIN_S0S1,
				 NRF_GPIO_PIN_NOSENSE);

#if DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, gnd_gpios)
	nrf_gpio_cfg(NRF_GPIO_PIN_MAP(GND_GPIO_PORT_NUM, GND_GPIO_PIN),
				 NRF_GPIO_PIN_DIR_OUTPUT,
				 NRF_GPIO_PIN_INPUT_DISCONNECT,
				 NRF_GPIO_PIN_NOPULL,
				 NRF_GPIO_PIN_D0H1,
				 NRF_GPIO_PIN_NOSENSE);
	nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(GND_GPIO_PORT_NUM, GND_GPIO_PIN));
#endif

#if DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, pwr_gpios)
	nrf_gpio_cfg(NRF_GPIO_PIN_MAP(PWR_GPIO_PORT_NUM, PWR_GPIO_PIN),
				 NRF_GPIO_PIN_DIR_OUTPUT,
				 NRF_GPIO_PIN_INPUT_DISCONNECT,
				 NRF_GPIO_PIN_NOPULL,
				 NRF_GPIO_PIN_D0H1,
				 NRF_GPIO_PIN_NOSENSE);
	nrf_gpio_pin_set(NRF_GPIO_PIN_MAP(PWR_GPIO_PORT_NUM, PWR_GPIO_PIN));
#endif

	return 0;
}

SYS_INIT(board_promicro_init, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);