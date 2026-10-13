#include "temperature_measurement.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/w1_sensor.h>
#include "canopennode.h"

LOG_MODULE_REGISTER(temperature_measurement, CONFIG_CANOPEN_LOG_LEVEL);

#if defined(CONFIG_SENSOR)
#include <zephyr/drivers/sensor.h>
#if DT_NODE_HAS_STATUS(DT_NODELABEL(temp_0), okay)
static const struct device *temp_sensor_dev = DEVICE_DT_GET(DT_NODELABEL(temp_0));
#else
static const struct device *temp_sensor_dev = NULL;
#endif

#if defined(CONFIG_APP_NATIVE_SIM_W1_EMUL)
static bool native_sim_temp_emulator_active;
static float native_sim_temp_emulator_value_c;
#endif
#endif

#if defined(CONFIG_SENSOR)

void w1_search_callback(struct w1_rom rom, void *user_data)
{
    uint64_t rom_id = w1_rom_to_uint64(&rom);
    LOG_INF("Device found; family: 0x%02x, serial: 0x%016llx", rom.family, rom_id);
    if (rom.family == 0x28) { // DS18B20 family code
        CO_LOCK_OD();
        OD_temperatureDS18B20ID = rom_id;
        CO_UNLOCK_OD();
    } else {
        LOG_WRN("Non-DS18B20 device found on bus, ignoring.");
    }
}

void temperature_search_sensor(void)
{
    const struct device *w1_dev = device_get_binding(DEVICE_DT_NAME(DT_NODELABEL(w1_0)));
	if (!device_is_ready(w1_dev)) {
		LOG_ERR("Device not ready");
		return;
	}
	int num_devices = w1_search_rom(w1_dev, w1_search_callback, NULL);
	LOG_INF("Number of devices found on bus: %d", num_devices);
}

int temperature_measurement_init(void)
{
    struct w1_rom w1rom;
    struct sensor_value value;

#if defined(CONFIG_APP_NATIVE_SIM_W1_EMUL)
    native_sim_temp_emulator_active = true;
    native_sim_temp_emulator_value_c = (float)CONFIG_APP_NATIVE_SIM_W1_EMUL_TEMP_C / 10.0f;
    LOG_INF("Using native_sim W1 emulator, emulated temperature %.1f C",
            (double)native_sim_temp_emulator_value_c);
    return 0;
#endif

    if (temp_sensor_dev == NULL) {
        LOG_ERR("The temp_sensor device is not defined.");
        return -ENODEV;
    }

    if (!device_is_ready(temp_sensor_dev)) {
        LOG_ERR("The temperature sensor device is not ready.");
        return -ENODEV;
    }

    LOG_INF("Temperature sensor (DS18B20) initialized successfully.");

    int i = 0;
    while (OD_temperatureDS18B20ID == 0 && i < 3) {
        LOG_INF("Searching for DS18B20 sensor, attempt %d...", i + 1);
        temperature_search_sensor();
        if (OD_temperatureDS18B20ID != 0) {
            break;
        }
        k_sleep(K_SECONDS(1));
        i++;
    }

    if (OD_temperatureDS18B20ID == 0) {
        LOG_ERR("No DS18B20 ID stored in OD after search attempts.");
        return -ENODEV;
    }

    w1_uint64_to_rom(OD_temperatureDS18B20ID, &w1rom);
    w1_rom_to_sensor_value(&w1rom, &value);
    sensor_attr_set(temp_sensor_dev, SENSOR_CHAN_ALL, SENSOR_ATTR_W1_ROM, &value);
    return 0;
}

int temperature_measurement_read(void)
{
    int err;
    struct sensor_value temp_value;

#if defined(CONFIG_APP_NATIVE_SIM_W1_EMUL)
    if (native_sim_temp_emulator_active) {
        float temperature = native_sim_temp_emulator_value_c;

        if (temperature < -50.0f) {
            temperature = -50.0f;
        } else if (temperature > 125.0f) {
            temperature = 125.0f;
        }

        CO_LOCK_OD();
        OD_temperatureRaw = (int16_t)(temperature * 1000);
        OD_temperatureMeasurement = temperature;
        CO_UNLOCK_OD();

        LOG_DBG("Temperature measurement (native_sim emulator): %.2f°C",
                (double)temperature);
        return 0;
    }
#endif

    if (temp_sensor_dev == NULL || !device_is_ready(temp_sensor_dev)) {
        LOG_ERR("Temperature sensor device not ready.");
        return -ENODEV;
    }

    err = sensor_sample_fetch(temp_sensor_dev);
    if (err != 0) {
        LOG_ERR("Failed to fetch temperature sample: %d", err);
        return err;
    }

    err = sensor_channel_get(temp_sensor_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp_value);
    if (err != 0) {
        LOG_ERR("Failed to get temperature value: %d", err);
        return err;
    }

    float temperature = (float)temp_value.val1 + (float)temp_value.val2 / 1000000.0f;

    if (temperature < -50.0f) {
        temperature = -50.0f;
    } else if (temperature > 125.0f) {
        temperature = 125.0f;
    }

    CO_LOCK_OD();
    OD_temperatureRaw = (int16_t)(temperature * 1000); // Store temperature multiplied by 1000 for three decimal places in OD
    OD_temperatureMeasurement = temperature; // Store actual float value in OD for monitoring
    CO_UNLOCK_OD();

    LOG_DBG("Temperature measurement: %.2f°C (raw: val1=%d val2=%d)",
            (double)temperature, temp_value.val1, temp_value.val2);
    return 0;
}

#else

int temperature_measurement_init(void)
{
    LOG_WRN("Temperature measurement disabled: SENSOR support is not enabled.");
    return -ENOTSUP;
}

int temperature_measurement_read(void)
{
    return -ENOTSUP;
}

#endif

float temperature_measurement_last_value(void)
{
    return OD_temperatureMeasurement;
}
