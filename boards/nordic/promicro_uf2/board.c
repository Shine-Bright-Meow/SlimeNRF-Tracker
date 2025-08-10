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

/* Early: turn on VCC (P0.13) and IMU power (P0.31 if used). No sleeps here. */
static int vcc_gate_early(void)
{
    /* VCC gate (P0.13) */
    nrf_gpio_cfg_output(13);
    nrf_gpio_pin_set(13);

    /* IMU power rail (P0.31) — only if your hardware feeds IMU VDD here */
    nrf_gpio_cfg_output(31);
    nrf_gpio_pin_set(31);

    printk("vcc_gate_early(): set P0.13=1, P0.31=1\n");
    return 0;
}
SYS_INIT(vcc_gate_early, PRE_KERNEL_1, 0);

/* Optional: small delay after the kernel starts, before probing IMU */
static int power_settle_delay(void)
{
    k_msleep(20);   // safe here
    return 0;
}
SYS_INIT(power_settle_delay, APPLICATION, 0);
