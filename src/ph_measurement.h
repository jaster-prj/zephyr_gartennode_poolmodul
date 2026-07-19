#ifndef _PH_MEASUREMENT_H
#define _PH_MEASUREMENT_H

#include <zephyr/kernel.h>

// #if defined(CONFIG_ADC)
// #if !DT_NODE_EXISTS(DT_NODELABEL(ph_sensor))
// #error "Overlay for ph_sensor ADC node not properly defined."
// #endif
// #endif

int ph_measurement_init(void);
int ph_measurement_read(void);
uint16_t *ph_measurement_alloc_samples(void);
void ph_measurement_free_samples(uint16_t *samples);
int ph_measurement_set_sample_count(uint8_t count);
float ph_measurement_last_value(void);

#endif /* _PH_MEASUREMENT_H */
