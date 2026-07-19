/*
 * Copyright (c) 2019 Vestas Wind Systems A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app);

#define LED0_NODE DT_ALIAS(led0)

#if !DT_NODE_HAS_STATUS(LED0_NODE, okay)
#error "Unsupported board: led0 devicetree alias is not defined"
#endif

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

int main_led(void)
{
    int ret;
    const uint16_t timeout = 500U;
    bool led_on = false;

    if (!device_is_ready(led0.port)) {
        LOG_ERR("LED0 device not ready");
        return 0;
    }

    ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        LOG_ERR("Failed to configure LED0 (%d)", ret);
        return 0;
    }

    while (true) {
        led_on = !led_on;
        gpio_pin_set_dt(&led0, led_on);
        k_sleep(K_MSEC(timeout));
    }
}
