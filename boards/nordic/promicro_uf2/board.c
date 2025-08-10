/*
 * Copyright (c) 2018 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>          // for k_msleep
#include <zephyr/sys/printk.h>      // for printk
#include <hal/nrf_gpio.h>

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)
#if DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, vcc_gpios)
#define VCC_PORT_NUM  DT_PROP(DT_GPIO_CTLR(ZEPHYR_USER_NODE, vcc_gpios), port)
#define VCC_PIN       DT_GPIO_PIN(ZEPHYR_USER_NODE, vcc_gpios)
static inline uint32_t vcc_abs_pin(void) { return NRF_GPIO_PIN_MAP(VCC_PORT_NUM, VCC_PIN); }

/* Turn VCC on as early as possible */
static int vcc_gate_early(void)
{
  /* VCC gate (P0.13) */
    nrf_gpio_cfg_output(13);
    nrf_gpio_pin_set(13);

    /* IMU power (P0.31) – only if your hardware feeds IMU VDD here */
    nrf_gpio_cfg_output(31);
    nrf_gpio_pin_set(31);

    k_msleep(20); // let the IMU finish POR before SPI talks to it
    return 0;
}
SYS_INIT(vcc_gate_early, PRE_KERNEL_1, 0);

/* Optional: prove control by blinking VCC after boot */
static int vcc_gate_late_test(void)
{
    k_msleep(400);           // let everything come up
    const uint32_t pin = vcc_abs_pin();
    nrf_gpio_pin_clear(pin); // VCC off
    k_msleep(150);
    nrf_gpio_pin_set(pin);   // VCC on
    printk("vcc_gate_late_test(): blinked VCC on abs pin %u\n", pin);
    return 0;
}
SYS_INIT(vcc_gate_late_test, APPLICATION, 99);
#endif

