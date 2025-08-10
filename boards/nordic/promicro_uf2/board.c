/*
 * Copyright (c) 2018 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/devicetree.h>
#include <hal/nrf_gpio.h>

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)
#define VCC_PORT_NUM  DT_PROP(DT_GPIO_CTLR(ZEPHYR_USER_NODE, vcc_gpios), port)
#define VCC_PIN       DT_GPIO_PIN(ZEPHYR_USER_NODE, vcc_gpios)

static int vcc_gate_early(void)
{
    const uint32_t pin = NRF_GPIO_PIN_MAP(VCC_PORT_NUM, VCC_PIN);
    nrf_gpio_cfg_output(pin);
    nrf_gpio_pin_set(pin);
    return 0;
}

SYS_INIT(vcc_gate_early, PRE_KERNEL_1, 0);