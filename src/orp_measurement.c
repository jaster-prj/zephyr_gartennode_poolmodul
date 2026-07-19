#include "orp_measurement.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include "common.h"
#include "canopennode.h"

LOG_MODULE_REGISTER(orp_measurement, CONFIG_CANOPEN_LOG_LEVEL);

#if defined(CONFIG_ADC)
#include <zephyr/drivers/adc.h>
static const struct adc_dt_spec orp_sensor = ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), orp_sensor);
static uint16_t *orp_sample_buffer;
static size_t orp_sample_count;
static struct k_mutex orp_mutex;
#endif

#define ORP_MEASUREMENT_MV_REF 3300U

#if defined(CONFIG_ADC)

uint16_t *orp_measurement_alloc_samples(void)
{
    orp_sample_count = (size_t)OD_ORPSamples;
    if (orp_sample_count == 0) {
        LOG_WRN("OD_ORPSamples is zero, allocating one sample buffer.");
        orp_sample_count = 1;
    }

    uint16_t *samples = k_calloc(orp_sample_count, sizeof(uint16_t));
    if (samples == NULL) {
        LOG_ERR("Could not allocate ORP sample array (%zu entries)", orp_sample_count);
    }
    return samples;
}

void orp_measurement_free_samples(uint16_t *samples)
{
    if (samples != NULL) {
        k_free(samples);
    }
}

int orp_measurement_set_sample_count(uint8_t count)
{
    if (count == 0) {
        return -EINVAL;
    }

    uint16_t *new_samples = k_calloc((size_t)count, sizeof(uint16_t));
    if (new_samples == NULL) {
        LOG_ERR("Could not allocate ORP sample array (%u entries)", count);
        return -ENOMEM;
    }

    k_mutex_lock(&orp_mutex, K_FOREVER);
    uint16_t *old = orp_sample_buffer;
    orp_sample_buffer = new_samples;
    orp_sample_count = (size_t)count;
    k_mutex_unlock(&orp_mutex);

    if (old != NULL) {
        k_free(old);
    }
    return 0;
}

int orp_measurement_init(void)
{
    int err;

    if (!adc_is_ready_dt(&orp_sensor)) {
        LOG_ERR("The orp_sensor ADC device is not ready.");
        return -ENODEV;
    }

    err = adc_channel_setup_dt(&orp_sensor);
    if (err != 0) {
        LOG_ERR("ORP sensor ADC channel setup failed: %d", err);
        return err;
    }

    orp_sample_buffer = orp_measurement_alloc_samples();
    if (orp_sample_buffer == NULL) {
        return -ENOMEM;
    }

    k_mutex_init(&orp_mutex);
    return 0;
}

int orp_measurement_read()
{
    int err;

    if (orp_sample_buffer == NULL) {
        LOG_ERR("ORP sample buffer is not allocated.");
        return -ENOMEM;
    }

    struct adc_sequence sequence = {
        .channels = BIT(orp_sensor.channel_id),
        .buffer = orp_sample_buffer,
        .buffer_size = orp_sample_count * sizeof(uint16_t),
        .resolution = orp_sensor.resolution,
    };

    /* lock buffer while performing ADC read and processing */
    k_mutex_lock(&orp_mutex, K_FOREVER);
    err = adc_read(orp_sensor.dev, &sequence);
    if (err != 0) {
        LOG_ERR("ORP sensor ADC read failed: %d", err);
        k_mutex_unlock(&orp_mutex);
        return err;
    }

    uint32_t raw = averagearray(orp_sample_buffer, orp_sample_count);
    k_mutex_unlock(&orp_mutex);
    float orp = ((30*(double)ORP_MEASUREMENT_MV_REF)-(75*raw*ORP_MEASUREMENT_MV_REF/1024))/75-OD_ORPOffset;

    if (orp < -1000.0f) {
        orp = -1000.0f;
    } else if (orp > 1000.0f) {
        orp = 1000.0f;
    }

    CO_LOCK_OD();
    OD_ORPMeasurement = (float)orp;
    CO_UNLOCK_OD();

    LOG_DBG("ORP measurement raw=%u ORP=%.2fmV", raw, (double)orp);
    return 0;
}

#else

uint16_t *orp_measurement_alloc_samples(void)
{
    return NULL;
}

void orp_measurement_free_samples(uint16_t *samples)
{
    ARG_UNUSED(samples);
}

int orp_measurement_init(void)
{
    LOG_WRN("ORP measurement disabled: ADC support is not enabled.");
    return -ENOTSUP;
}

int orp_measurement_read()
{
    return -ENOTSUP;
}

#endif

float orp_measurement_last_value(void)
{
    return OD_ORPMeasurement;
}