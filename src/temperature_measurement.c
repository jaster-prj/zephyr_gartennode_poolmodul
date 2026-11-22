#include "temperature_measurement.h"

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
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

static uint64_t assigned_rom_id;
#endif

#if defined(CONFIG_SENSOR)

struct temperature_discovery {
    uint64_t rom_id;
};

static void w1_search_callback(struct w1_rom rom, void *user_data)
{
    struct temperature_discovery *discovery = user_data;
    uint64_t rom_id = w1_rom_to_uint64(&rom);

    if (rom.family != 0x28) {
        LOG_WRN("Non-DS18B20 device found on bus, ignoring.");
        return;
    }
    LOG_INF("Found DS18B20: 0x%016llx", (unsigned long long)rom_id);
    if (discovery->rom_id == 0U) {
        discovery->rom_id = rom_id;
    }
}

static int temperature_assign_and_read(uint64_t rom_id,
                                       struct sensor_value *temperature)
{
    struct w1_rom rom;
    struct sensor_value address;
    int ret;

    w1_uint64_to_rom(rom_id, &rom);
    w1_rom_to_sensor_value(&rom, &address);
    ret = sensor_attr_set(temp_sensor_dev, SENSOR_CHAN_ALL,
                          SENSOR_ATTR_W1_ROM, &address);
    if (ret != 0) {
        return ret;
    }
    assigned_rom_id = rom_id;

    for (int attempt = 0; attempt < 3; attempt++) {
        ret = sensor_sample_fetch(temp_sensor_dev);
        if (ret == 0) {
            ret = sensor_channel_get(temp_sensor_dev,
                                     SENSOR_CHAN_AMBIENT_TEMP, temperature);
        }
        if (ret == 0) {
            return 0;
        }
        if (attempt < 2) {
            k_sleep(K_MSEC(100));
        }
    }
    return ret;
}

int temperature_measurement_init(void)
{
    struct sensor_value temperature;
    uint64_t configured_id;

    if (temp_sensor_dev == NULL || !device_is_ready(temp_sensor_dev)) {
        LOG_ERR("The temperature sensor device is not ready.");
        return -ENODEV;
    }

    CO_LOCK_OD();
    configured_id = OD_temperatureDS18B20ID;
    CO_UNLOCK_OD();

    /* Use the saved ROM directly; only scan if it fails three reads. */
    if (configured_id != 0U) {
        if (temperature_assign_and_read(configured_id, &temperature) == 0) {
            LOG_INF("Using configured DS18B20 0x%016llx",
                    (unsigned long long)configured_id);
            CO_LOCK_OD();
            OD_temperatureRaw = (int16_t)(temperature.val1 * 1000 +
                                          temperature.val2 / 1000);
            OD_temperatureMeasurement =
                (float)temperature.val1 + (float)temperature.val2 / 1000000.0f;
            CO_UNLOCK_OD();
            return 0;
        }

        LOG_WRN("Configured DS18B20 0x%016llx did not respond after 3 reads; clearing ID",
                (unsigned long long)configured_id);
        CO_LOCK_OD();
        OD_temperatureDS18B20ID = 0U;
        CO_UNLOCK_OD();
    }

    const struct device *w1_dev = DEVICE_DT_GET(DT_NODELABEL(w1_0));
    if (!device_is_ready(w1_dev)) {
        LOG_ERR("1-Wire bus device is not ready");
        return -ENODEV;
    }

    for (int attempt = 0; attempt < 3; attempt++) {
        struct temperature_discovery discovery = {0};
        int num_devices = w1_search_rom(w1_dev, w1_search_callback, &discovery);

        LOG_INF("DS18B20 discovery attempt %d found %d 1-Wire devices",
                attempt + 1, num_devices);
        if (discovery.rom_id != 0U &&
            temperature_assign_and_read(discovery.rom_id, &temperature) == 0) {
            CO_LOCK_OD();
            OD_temperatureDS18B20ID = discovery.rom_id;
            OD_temperatureRaw = (int16_t)(temperature.val1 * 1000 +
                                          temperature.val2 / 1000);
            OD_temperatureMeasurement =
                (float)temperature.val1 + (float)temperature.val2 / 1000000.0f;
            CO_UNLOCK_OD();
            LOG_INF("Stored newly discovered DS18B20 ID 0x%016llx",
                    (unsigned long long)discovery.rom_id);
            return 0;
        }
        if (attempt < 2) {
            k_sleep(K_SECONDS(1));
        }
    }

    LOG_ERR("No responding DS18B20 sensor found after discovery attempts");
    return -ENODEV;
}

int temperature_measurement_read(void)
{
    int err;
    struct sensor_value temp_value;
    uint64_t configured_id;

    if (temp_sensor_dev == NULL || !device_is_ready(temp_sensor_dev)) {
        LOG_ERR("Temperature sensor device not ready.");
        return -ENODEV;
    }

    CO_LOCK_OD();
    configured_id = OD_temperatureDS18B20ID;
    CO_UNLOCK_OD();
    if (configured_id != 0U && configured_id != assigned_rom_id) {
        struct w1_rom rom;
        struct sensor_value address;

        w1_uint64_to_rom(configured_id, &rom);
        w1_rom_to_sensor_value(&rom, &address);
        err = sensor_attr_set(temp_sensor_dev, SENSOR_CHAN_ALL,
                              SENSOR_ATTR_W1_ROM, &address);
        if (err != 0) {
            return err;
        }
        assigned_rom_id = configured_id;
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
