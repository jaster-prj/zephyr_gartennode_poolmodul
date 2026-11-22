#include "waterpump.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "canopennode.h"
#if defined(CONFIG_BOARD_NATIVE_SIM)
#include "native_sim_sensor.h"
#endif

LOG_MODULE_REGISTER(waterpump, CONFIG_CANOPEN_LOG_LEVEL);

static const struct gpio_dt_spec waterpump_sw =
	GPIO_DT_SPEC_GET_OR(DT_NODELABEL(waterpump_sw), gpios, {0});
static bool waterpump_state = false;

#if defined(CONFIG_BOARD_NATIVE_SIM)
static bool waterpump_state_reported;

static void publish_waterpump_state(void)
{
	const char *payload = waterpump_state ? "ON" : "OFF";
	size_t payload_len = waterpump_state ? 2U : 3U;

	if (native_sim_sensor_publish_state("/state/actor/waterpump",
					   (const uint8_t *)payload, payload_len) == 0) {
		waterpump_state_reported = true;
	}
}
#endif

int waterpump_init(void)
{
	int err;

	if (!gpio_is_ready_dt(&waterpump_sw)) {
		LOG_ERR("The waterpump switch GPIO port is not ready.");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&waterpump_sw, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		LOG_ERR("Configuring waterpump switch GPIO pin failed: %d", err);
		return err;
	}

	waterpump_state = false;
#if defined(CONFIG_BOARD_NATIVE_SIM)
	waterpump_state_reported = false;
#endif
	return 0;
}

int waterpump_on(void)
{
	int err;

	if (!gpio_is_ready_dt(&waterpump_sw)) {
		LOG_ERR("The waterpump switch GPIO port is not ready.");
		return -ENODEV;
	}

	err = gpio_pin_set_dt(&waterpump_sw, 1);
	if (err != 0) {
		LOG_ERR("Setting waterpump GPIO high failed: %d", err);
		return err;
	}

	waterpump_state = true;
	LOG_DBG("Waterpump turned on");
#if defined(CONFIG_BOARD_NATIVE_SIM)
	waterpump_state_reported = false;
	publish_waterpump_state();
#endif
	return 0;
}

int waterpump_off(void)
{
	int err;

	if (!gpio_is_ready_dt(&waterpump_sw)) {
		LOG_ERR("The waterpump switch GPIO port is not ready.");
		return -ENODEV;
	}

	err = gpio_pin_set_dt(&waterpump_sw, 0);
	if (err != 0) {
		LOG_ERR("Setting waterpump GPIO low failed: %d", err);
		return err;
	}

	waterpump_state = false;
	LOG_DBG("Waterpump turned off");
#if defined(CONFIG_BOARD_NATIVE_SIM)
	waterpump_state_reported = false;
	publish_waterpump_state();
#endif
	return 0;
}

bool waterpump_is_on(void)
{
	return waterpump_state;
}

int waterpump_process(void)
{
	bool enable = false;

	CO_LOCK_OD();
	if (OD_sensorPumpEnable == 1) {
		enable = true;
	}
	CO_UNLOCK_OD();

	if (enable && !waterpump_state) {
		return waterpump_on();
	}
	if (!enable && waterpump_state) {
		return waterpump_off();
	}

#if defined(CONFIG_BOARD_NATIVE_SIM)
	/* Retry initial feedback until a valid input establishes the MQTT root. */
	if (!waterpump_state_reported) {
		publish_waterpump_state();
	}
#endif
	return 0;
}
