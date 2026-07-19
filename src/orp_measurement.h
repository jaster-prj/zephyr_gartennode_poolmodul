#ifndef _ORP_MEASUREMENT_H
#define _ORP_MEASUREMENT_H

#include <zephyr/kernel.h>

// #if defined(CONFIG_ADC)
// #if !DT_NODE_EXISTS(DT_NODELABEL(orp_sensor))
// #error "Overlay for orp_sensor ADC node not properly defined."
// #endif
// #endif

int orp_measurement_init(void);
int orp_measurement_read();
uint16_t *orp_measurement_alloc_samples(void);
void orp_measurement_free_samples(uint16_t *samples);
int orp_measurement_set_sample_count(uint8_t count);
float orp_measurement_last_value(void);

#endif /* _ORP_MEASUREMENT_H */
