#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <canopennode.h>
#include "sensor.h"
#include "ph_measurement.h"
#include "orp_measurement.h"
#include "temperature_measurement.h"

#define SENSOR_STACK 2048

LOG_MODULE_REGISTER(sensor, CONFIG_CANOPEN_LOG_LEVEL);
K_THREAD_STACK_DEFINE(sensor_stack_area, SENSOR_STACK);

static bool sensor_loop = false;
static _thread_t sensor_thread;
static k_tid_t sensor_tid;

/* GPIO used to switch power to external sensors (MOSFET gate) */
static struct gpio_dt_spec sensor_sw = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sensor_sw), gpios, {0});

/**
 * @brief Initialize sensor measurements (pH, ORP, temperature)
 */
int sensor_init(void)
{
	int err;

	err = ph_measurement_init();
	if (err != 0) {
		LOG_WRN("pH measurement initialization: %d", err);
	}

	err = orp_measurement_init();
	if (err != 0) {
		LOG_WRN("ORP measurement initialization: %d", err);
	}

	/* Configure the sensor power GPIO if available */
	if (gpio_is_ready_dt(&sensor_sw)) {
		err = gpio_pin_configure_dt(&sensor_sw, GPIO_OUTPUT_INACTIVE);
		if (err != 0) {
			LOG_WRN("Configuring sensor power GPIO failed: %d", err);
		}
	} else {
		LOG_DBG("Sensor power GPIO not ready or not defined in DT");
	}

	return 0;
}

/**
 * @brief Enable or disable sensor thread
 *
 * @param value true to enable, false to disable
 * @return 0 on success, -1 on error, 1 if already started
 */
int sensor_enable(bool value)
{
	if (value) {
		LOG_DBG("sensor_enable called with value: %d", value);
		if (OD_sensorEnable == 1) {
			return 1;
		}
		/* Power sensors via MOSFET */
		if (gpio_is_ready_dt(&sensor_sw)) {
			int _err = gpio_pin_set_dt(&sensor_sw, 1);
			if (_err != 0) {
				LOG_WRN("Failed to set sensor power GPIO: %d", _err);
			}
		}

		sensor_loop = true;
		sensor_tid = k_thread_create(&sensor_thread, sensor_stack_area,
								 K_THREAD_STACK_SIZEOF(sensor_stack_area),
								 sensor_func,
								 NULL, NULL, NULL,
								 K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);
	} else {
		LOG_DBG("sensor_enable called with value: %d", value);
		sensor_loop = false;
		/* Switch off sensor power */
		if (gpio_is_ready_dt(&sensor_sw)) {
			int _err = gpio_pin_set_dt(&sensor_sw, 0);
			if (_err != 0) {
				LOG_WRN("Failed to clear sensor power GPIO: %d", _err);
			}
		}
	}
	return 0;
}

/**
 * @brief Sensor thread function - reads pH, ORP, and temperature sensors
 *
 * @param d0 Unused
 * @param d1 Unused
 * @param d2 Unused
 */
void sensor_func(void *d0, void *d1, void *d2) {

	int temperr;
	(void)d0;
	(void)d1;
	(void)d2;

	LOG_DBG("start sensor thread");
	CO_LOCK_OD();
	OD_sensorEnable = 1;
	CO_UNLOCK_OD();
	LOG_DBG("Sensor Period: %d ms", OD_sensorPeriode);
	LOG_DBG("start loop");

	temperr = temperature_measurement_init();
	if (temperr != 0) {
		LOG_WRN("Temperature measurement initialization: %d", temperr);
	}

	while(sensor_loop) {
		float ph_value = 0.0f;
		float orp_value = 0.0f;
		float temperature_value = 0.0f;

		if (ph_measurement_read() == 0) {
			ph_value = ph_measurement_last_value();
			LOG_INF("pH: %.2f", (double)ph_value);
		}

		if (orp_measurement_read() == 0) {
			orp_value = orp_measurement_last_value();
			LOG_INF("ORP: %.2f mV", (double)orp_value);
		}

		if (temperr == 0 && temperature_measurement_read() == 0) {
			temperature_value = temperature_measurement_last_value();
			LOG_INF("Temperature: %.2f°C", (double)temperature_value);
		}

		k_sleep(K_MSEC(OD_sensorPeriode));
	}

	LOG_DBG("stop sensor thread");
	CO_LOCK_OD();
	OD_sensorEnable = 0;
	CO_UNLOCK_OD();
	/* Ensure sensors are powered down when thread stops */
	if (gpio_is_ready_dt(&sensor_sw)) {
		int _err = gpio_pin_set_dt(&sensor_sw, 0);
		if (_err != 0) {
			LOG_WRN("Failed to clear sensor power GPIO on stop: %d", _err);
		}
	}
}

/**
 *  @brief process sensor updates
 */
int sensor_process(void) {
	return 0;
}
