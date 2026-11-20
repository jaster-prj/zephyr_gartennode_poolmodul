#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>
#include <canopennode.h>
#include "sensor.h"
#include "ph_measurement.h"
#include "orp_measurement.h"
#include "temperature_measurement.h"
#if defined(CONFIG_BOARD_NATIVE_SIM)
#include "native_sim_sensor.h"
#include <zephyr/sys/printk.h>
#endif

#define SENSOR_STACK 2048

LOG_MODULE_REGISTER(sensor, CONFIG_CANOPEN_LOG_LEVEL);
K_THREAD_STACK_DEFINE(sensor_stack_area, SENSOR_STACK);

static atomic_t sensor_loop;
static atomic_t sensor_thread_running;
static _thread_t sensor_thread;
static k_tid_t sensor_tid;
#if defined(CONFIG_BOARD_NATIVE_SIM)
K_SEM_DEFINE(native_sim_sensor_wakeup, 0, 1);
#endif

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
		LOG_INF("Enabling pool sensors");
		atomic_set(&sensor_loop, 1);
		/* Power sensors via MOSFET */
		if (gpio_is_ready_dt(&sensor_sw)) {
			int _err = gpio_pin_set_dt(&sensor_sw, 1);
			if (_err != 0) {
				LOG_WRN("Failed to set sensor power GPIO: %d", _err);
			}
		}
		CO_LOCK_OD();
		OD_sensorEnable = 1;
		CO_UNLOCK_OD();

		if (!atomic_cas(&sensor_thread_running, 0, 1)) {
#if defined(CONFIG_BOARD_NATIVE_SIM)
			sensor_wakeup();
#else
			if (sensor_tid != NULL) {
				k_wakeup(sensor_tid);
			}
#endif
			return 1;
		}

		sensor_tid = k_thread_create(&sensor_thread, sensor_stack_area,
							 K_THREAD_STACK_SIZEOF(sensor_stack_area),
							 sensor_func, NULL, NULL, NULL,
							 K_PRIO_PREEMPT(-1), 0, K_NO_WAIT);
		if (sensor_tid == NULL) {
			atomic_set(&sensor_thread_running, 0);
			atomic_set(&sensor_loop, 0);
			CO_LOCK_OD();
			OD_sensorEnable = 0;
			CO_UNLOCK_OD();
			return -1;
		}
		LOG_INF("Pool sensor thread started");
	} else {
		LOG_INF("Disabling pool sensors");
		atomic_set(&sensor_loop, 0);
		CO_LOCK_OD();
		OD_sensorEnable = 0;
		CO_UNLOCK_OD();
#if defined(CONFIG_BOARD_NATIVE_SIM)
		sensor_wakeup();
#else
		if (sensor_tid != NULL) {
			k_wakeup(sensor_tid);
		}
#endif
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

	LOG_INF("Pool sensor measurement loop running");
	LOG_DBG("Sensor Period: %d ms", OD_sensorPeriode);
	LOG_DBG("start loop");

	temperr = temperature_measurement_init();
	if (temperr != 0) {
		LOG_WRN("Temperature measurement initialization: %d", temperr);
	}

	while (atomic_get(&sensor_loop)) {
		float ph_value = 0.0f;
		float orp_value = 0.0f;
		float temperature_value = 0.0f;

		if (ph_measurement_read() == 0) {
			ph_value = ph_measurement_last_value();
			LOG_INF("pH: %.2f", (double)ph_value);
#if defined(CONFIG_BOARD_NATIVE_SIM)
			char state[16];
			int32_t scaled = (int32_t)(ph_value * 100.0f);
			int len = snprintk(state, sizeof(state), "%d.%02d",
					   scaled / 100, (scaled % 100) < 0 ? -(scaled % 100) : (scaled % 100));

			if (len > 0) {
				(void)native_sim_sensor_publish_state("/state/measurement/ph",
					(const uint8_t *)state, (size_t)len);
			}
#endif
		}

		if (orp_measurement_read() == 0) {
			orp_value = orp_measurement_last_value();
			LOG_INF("ORP: %.2f mV", (double)orp_value);
#if defined(CONFIG_BOARD_NATIVE_SIM)
			char state[20];
			int32_t scaled = (int32_t)(orp_value * 100.0f);
			int len = snprintk(state, sizeof(state), "%s%d.%02d",
					   scaled < 0 ? "-" : "", (scaled < 0 ? -scaled : scaled) / 100,
					   (scaled % 100) < 0 ? -(scaled % 100) : (scaled % 100));

			if (len > 0) {
				(void)native_sim_sensor_publish_state("/state/measurement/orp",
					(const uint8_t *)state, (size_t)len);
			}
#endif
		}

		if (temperr == 0 && temperature_measurement_read() == 0) {
			temperature_value = temperature_measurement_last_value();
			LOG_INF("Temperature: %.2f°C", (double)temperature_value);
		}

#if defined(CONFIG_BOARD_NATIVE_SIM)
		(void)k_sem_take(&native_sim_sensor_wakeup, K_MSEC(OD_sensorPeriode));
#else
		k_sleep(K_MSEC(OD_sensorPeriode));
#endif
	}

	LOG_INF("Pool sensor measurement loop stopped");
	if (!atomic_get(&sensor_loop)) {
		CO_LOCK_OD();
		OD_sensorEnable = 0;
		CO_UNLOCK_OD();
	}
	/* Ensure sensors are powered down when thread stops */
	if (gpio_is_ready_dt(&sensor_sw)) {
		int _err = gpio_pin_set_dt(&sensor_sw, 0);
		if (_err != 0) {
			LOG_WRN("Failed to clear sensor power GPIO on stop: %d", _err);
		}
	}
	atomic_set(&sensor_thread_running, 0);
}

/**
 *  @brief process sensor updates
 */
int sensor_process(void) {
	return 0;
}
#if defined(CONFIG_BOARD_NATIVE_SIM)
void sensor_wakeup(void) { k_sem_give(&native_sim_sensor_wakeup); }
#endif
