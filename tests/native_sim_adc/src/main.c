#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "native_sim_sensor.h"

#define PH_ADC ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), ph_sensor)
#define ORP_ADC ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), orp_sensor)

static int read_millivolts(const struct adc_dt_spec *spec, int32_t *millivolts)
{
    uint16_t sample = 0;
    struct adc_sequence sequence = {
        .channels = BIT(spec->channel_id),
        .buffer = &sample,
        .buffer_size = sizeof(sample),
        .resolution = spec->resolution,
    };
    int32_t raw = 0;
    int err = adc_read(spec->dev, &sequence);
    if (err != 0) return err;
    raw = sample;
    err = adc_raw_to_millivolts_dt(spec, &raw);
    if (err == 0) *millivolts = raw;
    return err;
}

static int check_model_transfer(void)
{
    uint32_t mv;
    if (native_sim_ph_voltage_mv(0.0, 150, 600, 3300, &mv) != 0 || mv != 600) return -1;
    if (native_sim_ph_voltage_mv(14.0, 150, 600, 3300, &mv) != 0 || mv != 2700) return -1;
    if (native_sim_ph_voltage_mv(-0.01, 150, 600, 3300, &mv) == 0) return -1;
    if (native_sim_ph_voltage_mv(14.01, 150, 600, 3300, &mv) == 0) return -1;
    if (native_sim_ph_voltage_mv(14.0, 300, 1000, 3300, &mv) == 0) return -1;
    if (native_sim_orp_voltage_mv(-1000, 1000, 1650, 3300, &mv) != 0 || mv != 650) return -1;
    if (native_sim_orp_voltage_mv(1000, 1000, 1650, 3300, &mv) != 0 || mv != 2650) return -1;
    if (native_sim_orp_voltage_mv(1001, 1000, 1650, 3300, &mv) == 0) return -1;
    if (native_sim_orp_voltage_mv(1000, 2000, 1650, 3300, &mv) == 0) return -1;
    return 0;
}

int main(void)
{
    const struct adc_dt_spec ph = PH_ADC;
    const struct adc_dt_spec orp = ORP_ADC;
    uint32_t modeled_mv;
    int32_t measured_mv;

    if (!adc_is_ready_dt(&ph) || !adc_is_ready_dt(&orp) ||
        adc_channel_setup_dt(&ph) != 0 || adc_channel_setup_dt(&orp) != 0 ||
        check_model_transfer() != 0) {
        printk("ADC or sensor model setup failed\n");
        return 1;
    }
    if (native_sim_ph_voltage_mv(7.25, 150, 600, 3300, &modeled_mv) != 0 ||
        adc_emul_const_value_set(ph.dev, ph.channel_id, modeled_mv) != 0 ||
        read_millivolts(&ph, &measured_mv) != 0 ||
        measured_mv - (int32_t)modeled_mv > 1 || measured_mv - (int32_t)modeled_mv < -1) {
        printk("pH ADC quantization check failed\n");
        return 1;
    }
    if (adc_emul_const_value_set(ph.dev, ph.channel_id, 500) != 0 ||
        read_millivolts(&ph, &measured_mv) != 0 || measured_mv < 498 || measured_mv > 502 ||
        adc_emul_const_value_set(ph.dev, ph.channel_id, 1500) != 0 ||
        read_millivolts(&ph, &measured_mv) != 0 || measured_mv < 1498 || measured_mv > 1502) {
        printk("pH ADC runtime updates failed\n");
        return 1;
    }
    if (adc_emul_const_value_set(orp.dev, orp.channel_id, 800) != 0 ||
        read_millivolts(&orp, &measured_mv) != 0 || measured_mv < 798 || measured_mv > 802 ||
        adc_emul_const_value_set(orp.dev, orp.channel_id, 2600) != 0 ||
        read_millivolts(&orp, &measured_mv) != 0 || measured_mv < 2598 || measured_mv > 2602) {
        printk("ORP ADC runtime updates failed\n");
        return 1;
    }
    if (native_sim_sensor_init() != 0) {
        printk("native-sim input receiver init failed\n");
        return 1;
    }
    printk("native-sim input receiver ready\n");
    for (uint32_t attempts = 0; attempts < 100000 &&
         native_sim_sensor_ipc_packet_count() < 5; attempts++) {
        if (native_sim_sensor_process() < 0) {
            printk("native-sim input poll failed\n");
            return 1;
        }
        k_yield();
    }
    if (native_sim_sensor_ipc_packet_count() < 5) {
        printk("receiver packet count failed: %u\n", native_sim_sensor_ipc_packet_count());
        return 1;
    }
    printk("reading pH channel\n");
    if (read_millivolts(&ph, &measured_mv) != 0) {
        printk("pH receiver read failed\n");
        return 1;
    }
    printk("receiver pH mv=%d\n", measured_mv);
    if (measured_mv < 1799 || measured_mv > 1801) {
        printk("pH receiver value incorrect\n");
        return 1;
    }
    printk("reading ORP channel\n");
    if (read_millivolts(&orp, &measured_mv) != 0) {
        printk("ORP receiver read failed\n");
        return 1;
    }
    printk("receiver ORP mv=%d\n", measured_mv);
    if (measured_mv < 1897 || measured_mv > 1902) {
        printk("native-sim receiver update/preserve check failed\n");
        return 1;
    }
    if (native_sim_sensor_publish_state("/state/measurement/ph",
            (const uint8_t *)"7.25", 4) != 0 ||
        native_sim_sensor_publish_state("/state/actor/waterpump",
            (const uint8_t *)"ON", 2) != 0) {
        printk("native-sim state event send failed\n");
        return 1;
    }
    printk("native-sim receiver updates and invalid-input preservation passed\n");
    return 0;
}
