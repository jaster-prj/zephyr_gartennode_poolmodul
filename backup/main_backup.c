/*
 * Copyright (c) 2019 Vestas Wind Systems A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
 #include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/settings/settings.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/w1_sensor.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/printk.h>
#include <canopennode.h>
#include "watchdog.h"
#include "sensor.h"
#include "orp_measurement.h"
#include "ph_measurement.h"
#include "waterpump.h"
#include "led.h"

#define LOG_LEVEL CONFIG_CANOPEN_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app);

static CO_SDO_abortCode_t odf_2102(CO_ODF_arg_t *odf_arg);
static CO_SDO_abortCode_t odf_2103(CO_ODF_arg_t *odf_arg);
static CO_SDO_abortCode_t odf_2108(CO_ODF_arg_t *odf_arg);
static CO_SDO_abortCode_t odf_2112(CO_ODF_arg_t *odf_arg);

#define STORAGE_PARTITION	storage_partition
#define STORAGE_PARTITION_ID	FIXED_PARTITION_ID(STORAGE_PARTITION)

#define CAN_INTERFACE DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus))
#define CAN_BITRATE (DT_PROP_OR(DT_CHOSEN(zephyr_canbus), bitrate, \
					  DT_PROP_OR(DT_CHOSEN(zephyr_canbus), bus_speed, \
						     CONFIG_CAN_DEFAULT_BITRATE)) / 1000)


/**
 * @brief Watchdog callback function
 *
 * @param channel_id Watchdog channel ID
 * @param user_data Pointer to user data (thread ID)
 */
static void main_watchdog_callback(int channel_id, void *user_data)
{
    LOG_ERR("Watchdog channel %d callback, user data: %p",
        channel_id, k_thread_name_get((k_tid_t)user_data));
	sys_reboot(SYS_REBOOT_COLD);
}

/**
 * @brief Configure LED indicators pins and callbacks.
 *
 * This routine configures the GPIOs for the red and green LEDs (if
 * available).
 *
 * @param nmt CANopenNode NMT object.
 */
static void initialize()
{
	if (led_init() != 0) {
		LOG_ERR("LED initialization failed");
	}
	if (sensor_init() != 0) {
		LOG_ERR("Sensor initialization failed");
	}
	if (waterpump_init() != 0) {
		LOG_ERR("Waterpump initialization failed");
	}
	// if (watchdog_init() != 0) {
	// 	LOG_ERR("Watchdog initialization failed");
	// }
}

/**
 * @brief Set sensor Enable
 *
 * @param odf_arg SDO argument structure.
 * @return SDO abort code.
 */
static CO_SDO_abortCode_t odf_2102(CO_ODF_arg_t *odf_arg)
{
	if (odf_arg->reading) {
		return CO_SDO_AB_NONE;
	}

	uint8_t value;

	if (odf_arg->data == NULL) {
		return CO_SDO_AB_GENERAL;
	}
	value = *odf_arg->data;
	
	int ret = sensor_enable(value);
	LOG_DBG("sensor_enable returned %d", ret);
	if (ret == 0 || ret == 1) {
		return CO_SDO_AB_NONE;
	}
	return CO_SDO_AB_GENERAL;
}

/**
 * @brief Set sensor period
 *
 * @param odf_arg SDO argument structure.
 * @return SDO abort code.
 */
static CO_SDO_abortCode_t odf_2103(CO_ODF_arg_t *odf_arg)
{
	if (odf_arg->reading) {
		return CO_SDO_AB_NONE;
	}
	if (odf_arg->data == NULL) {
		return CO_SDO_AB_GENERAL;
	}
	uint8_t value;
	value = *odf_arg->data;
	LOG_DBG("Set sensor period to %d ms", value);
	return CO_SDO_AB_NONE;
}

/**
 * @brief Set pH sample count
 *
 * This SDO allows updating the number of samples used for pH averaging.
 */
static CO_SDO_abortCode_t odf_2108(CO_ODF_arg_t *odf_arg)
{
	if (odf_arg->reading) {
		return CO_SDO_AB_NONE;
	}

	if (odf_arg->data == NULL) {
		return CO_SDO_AB_GENERAL;
	}

	uint8_t value = *odf_arg->data;

	if (value == 0) {
		LOG_ERR("Invalid pH sample count: 0");
		return CO_SDO_AB_GENERAL;
	}

	CO_LOCK_OD();
	OD_PHSamples = value;
	CO_UNLOCK_OD();

	int ret = ph_measurement_set_sample_count(value);
	LOG_DBG("ph_measurement_set_sample_count returned %d", ret);
	if (ret == 0) {
		return CO_SDO_AB_NONE;
	}
	return CO_SDO_AB_GENERAL;
}

/**
 * @brief Set orp sample count
 *
 * @param odf_arg SDO argument structure.
 * @return SDO abort code.
 */
static CO_SDO_abortCode_t odf_2112(CO_ODF_arg_t *odf_arg)
{
	if (odf_arg->reading) {
		return CO_SDO_AB_NONE;
	}

	if (odf_arg->data == NULL) {
		return CO_SDO_AB_GENERAL;
	}

	uint8_t value = *odf_arg->data;

	/* value represents the number of ORP samples to average */
	if (value == 0) {
		LOG_ERR("Invalid ORP sample count: 0");
		return CO_SDO_AB_GENERAL;
	}

	/* Update EEPROM value under OD lock */
	CO_LOCK_OD();
	OD_ORPSamples = value;
	CO_UNLOCK_OD();

	/* Ask ORP module to resize its sample buffer. This function uses
	 * an internal mutex to prevent races with measurements.
	 */
	int ret = orp_measurement_set_sample_count(value);
	LOG_DBG("orp_measurement_set_sample_count returned %d", ret);
	if (ret == 0) {
		return CO_SDO_AB_NONE;
	}
	return CO_SDO_AB_GENERAL;
}

/**
 * @brief Main application entry point.
 *
 * The main application thread is responsible for initializing the
 * CANopen stack and doing the non real-time processing.
 */
int main_backup(void)
{
	CO_NMT_reset_cmd_t reset = CO_RESET_NOT;
	CO_ReturnError_t err;
	struct canopen_context can;
	uint16_t timeout;
	uint32_t elapsed;
	int64_t timestamp;
	int task_id = -1;
#ifdef CONFIG_CANOPENNODE_STORAGE
	int ret;
#endif /* CONFIG_CANOPENNODE_STORAGE */
	can.dev = CAN_INTERFACE;
	if (!device_is_ready(can.dev)) {
		LOG_ERR("CAN interface not ready");
		return 0;
	}

#ifdef CONFIG_CANOPENNODE_STORAGE
	LOG_INF("Initializing settings subsystem\n");
	ret = settings_subsys_init();
	if (ret) {
		LOG_ERR("failed to initialize settings subsystem (err = %d)",
			ret);
		int error = 0;
		const struct flash_area *storage = NULL;
		// Open the partition
		error = flash_area_open(PARTITION_ID(storage_partition), &storage);
		if (error < 0) {
			return error;
		}

		// Erase the first sector (e.g., 4KB)
		error = flash_area_erase(storage, 0, PARTITION_SIZE(storage_partition));
		if (error < 0) {
			return error;
		}
		sys_reboot(SYS_REBOOT_COLD);
	}

	ret = settings_load();
	if (ret) {
		LOG_ERR("failed to load settings (err = %d)", ret);
		return 0;
	}
#endif /* CONFIG_CANOPENNODE_STORAGE */

	OD_powerOnCounter++;

	//task_id = watchdog_add(2000, main_watchdog_callback, NULL);
	if (task_id < 0) {
		LOG_ERR("Failed to add watchdog channel");
	}

	while (reset != CO_RESET_APP) {
		elapsed =  0U; /* milliseconds */

		LOG_INF("CANopen stack initialize");
		err = CO_init(&can, CONFIG_CANOPEN_NODE_ID, CAN_BITRATE);
		if (err != CO_ERROR_NO) {
			LOG_ERR("CO_init failed (err = %d)", err);
			return 0;
		}
		LOG_INF("CANopen stack initialized");

#ifdef CONFIG_CANOPENNODE_STORAGE
		canopen_storage_attach(CO->SDO[0], CO->em);
#endif /* CONFIG_CANOPENNODE_STORAGE */

		initialize();
		led_configure(CO->NMT);
		CO_OD_configure(CO->SDO[0], OD_2102_sensorEnable,
				odf_2102, NULL, 0U, 0U);
		CO_OD_configure(CO->SDO[0], OD_2103_sensorPeriode,
				odf_2103, NULL, 0U, 0U);
		CO_OD_configure(CO->SDO[0], OD_2108_PHSamples,
				odf_2108, NULL, 0U, 0U);
		CO_OD_configure(CO->SDO[0], OD_2112_ORPSamples,
				odf_2112, NULL, 0U, 0U);

		if (IS_ENABLED(CONFIG_CANOPENNODE_PROGRAM_DOWNLOAD)) {
			canopen_program_download_attach(CO->NMT, CO->SDO[0],
							CO->em);
		}

		sensor_enable(OD_sensorEnable);

		CO_CANsetNormalMode(CO->CANmodule[0]);
		struct sCO_OD_EEPROM eeprom_old = CO_OD_EEPROM;

		while (true) {
			timeout = 1U; /* default timeout in milliseconds */
			timestamp = k_uptime_get();
			reset = CO_process(CO, (uint16_t)elapsed, &timeout);

			if (reset != CO_RESET_NOT) {
				break;
			}

			if (timeout > 0) {
				sensor_process();
				waterpump_process();
#ifdef CONFIG_CANOPENNODE_STORAGE
				ret = memcmp(&eeprom_old, &CO_OD_EEPROM, sizeof(CO_OD_EEPROM));
				if (ret != 0) {
					LOG_DBG("EEPROM changed, saving to flash");
					memcpy(&eeprom_old, &CO_OD_EEPROM, sizeof(CO_OD_EEPROM));
					ret = canopen_storage_save(
						CANOPEN_STORAGE_EEPROM);
					if (ret) {
						LOG_ERR("failed to save EEPROM");
					}
				}

#endif /* CONFIG_CANOPENNODE_STORAGE */
				if (task_id >= 0) {
					int ret = watchdog_feed(task_id);
					if (ret != 0) {
						LOG_ERR("Failed to feed watchdog channel %d: %d", task_id, ret);
					}
				}
				/*
				 * Try to sleep for as long as the
				 * stack requested and calculate the
				 * exact time elapsed.
				 */
				k_sleep(K_MSEC(timeout));
				elapsed = (uint32_t)k_uptime_delta(&timestamp);
			} else {
				/*
				 * Do not sleep, more processing to be
				 * done by the stack.
				 */
				elapsed = 0U;
			}
		}

        sensor_enable(false);

		if (reset == CO_RESET_COMM) {
			LOG_INF("Resetting communication");
		}
	}
	if (task_id >= 0) {
		int ret = watchdog_delete(task_id);
		if (ret != 0) {
			LOG_ERR("Failed to delete watchdog channel %d: %d", task_id, ret);
		}
	}

	LOG_INF("Resetting device");

	CO_delete(&can);
	sys_reboot(SYS_REBOOT_COLD);
}