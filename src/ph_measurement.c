#include "ph_measurement.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include "canopennode.h"
#include "common.h"

LOG_MODULE_REGISTER(ph_measurement, CONFIG_CANOPEN_LOG_LEVEL);

#if defined(CONFIG_ADC)
#include <zephyr/drivers/adc.h>
static const struct adc_dt_spec ph_sensor = ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), ph_sensor);
static uint16_t *ph_sample_buffer;
static size_t ph_sample_count;
static struct k_mutex ph_mutex;
#endif

#define PH_MEASUREMENT_MV_REF 3300U
#define PH_MEASUREMENT_MV_NEUTRAL 1650U

#if defined(CONFIG_ADC)

uint16_t *ph_measurement_alloc_samples(void)
{
    ph_sample_count = (size_t)OD_PHSamples;
    if (ph_sample_count == 0) {
        LOG_WRN("OD_PHSamples is zero, allocating one sample buffer.");
        ph_sample_count = 1;
    }

    uint16_t *samples = k_calloc(ph_sample_count, sizeof(uint16_t));
    if (samples == NULL) {
        LOG_ERR("Could not allocate pH sample array (%zu entries)", ph_sample_count);
    }
    return samples;
}

void ph_measurement_free_samples(uint16_t *samples)
{
    if (samples != NULL) {
        k_free(samples);
    }
}

int ph_measurement_init(void)
{
    int err;

    if (!adc_is_ready_dt(&ph_sensor)) {
        LOG_ERR("The ph_sensor ADC device is not ready.");
        return -ENODEV;
    }

    err = adc_channel_setup_dt(&ph_sensor);
    if (err != 0) {
        LOG_ERR("pH sensor ADC channel setup failed: %d", err);
        return err;
    }

    ph_sample_buffer = ph_measurement_alloc_samples();
    if (ph_sample_buffer == NULL) {
        return -ENOMEM;
    }
    k_mutex_init(&ph_mutex);
    return 0;
}

int ph_measurement_read(void)
{
    int err;

    if (ph_sample_buffer == NULL) {
        LOG_ERR("pH sample buffer is not allocated.");
        return -ENOMEM;
    }

    struct adc_sequence sequence = {
        .channels = BIT(ph_sensor.channel_id),
        .buffer = ph_sample_buffer,
        .buffer_size = ph_sample_count * sizeof(uint16_t),
        .resolution = ph_sensor.resolution,
    };

    /* lock buffer while reading and processing */
    k_mutex_lock(&ph_mutex, K_FOREVER);
    err = adc_read(ph_sensor.dev, &sequence);
    if (err != 0) {
        LOG_ERR("pH sensor ADC read failed: %d", err);
        k_mutex_unlock(&ph_mutex);
        return err;
    }

    /* Calculate average of samples */
    double avg_raw = averagearray(ph_sample_buffer, (int)ph_sample_count);
    k_mutex_unlock(&ph_mutex);
    float ph = (float)(OD_PHKalibPoints[0]/10) + ((float)(avg_raw - OD_PHKalibValues[0]) * ((float)(OD_PHKalibPoints[1]/10) / (float)(OD_PHKalibValues[1])));

    if (ph < 0.0f) {
        ph = 0.0f;
    } else if (ph > 14.0f) {
        ph = 14.0f;
    }

    CO_LOCK_OD();
    OD_PHRaw = (uint16_t)avg_raw; // Store raw average in OD for monitoring
    OD_PHMeasurement = (float32_t)ph; // Store pH value multiplied by 100 for two decimal places in OD
    CO_UNLOCK_OD();

    LOG_DBG("pH measurement avg_raw=%.1f pH=%.2f", avg_raw, (double)ph);
    return 0;
}

int ph_measurement_set_sample_count(uint8_t count)
{
    if (count == 0) {
        return -EINVAL;
    }

    uint16_t *new_samples = k_calloc((size_t)count, sizeof(uint16_t));
    if (new_samples == NULL) {
        LOG_ERR("Could not allocate pH sample array (%u entries)", count);
        return -ENOMEM;
    }

    k_mutex_lock(&ph_mutex, K_FOREVER);
    uint16_t *old = ph_sample_buffer;
    ph_sample_buffer = new_samples;
    ph_sample_count = (size_t)count;
    k_mutex_unlock(&ph_mutex);

    if (old != NULL) {
        k_free(old);
    }
    return 0;
}

#else

uint16_t *ph_measurement_alloc_samples(void)
{
    return NULL;
}

void ph_measurement_free_samples(uint16_t *samples)
{
    ARG_UNUSED(samples);
}

int ph_measurement_init(void)
{
    LOG_WRN("pH measurement disabled: ADC support is not enabled.");
    return -ENOTSUP;
}

int ph_measurement_read(void)
{
    return -ENOTSUP;
}

#endif

float ph_measurement_last_value(void)
{
    return (float)OD_PHMeasurement;
}
