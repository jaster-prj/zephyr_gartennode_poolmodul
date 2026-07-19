#ifndef _TEMPERATURE_MEASUREMENT_H
#define _TEMPERATURE_MEASUREMENT_H

#include <zephyr/kernel.h>

#if defined(CONFIG_SENSOR)
#if !DT_NODE_EXISTS(DT_NODELABEL(temp_0))
#error "Overlay for temp_0 (DS18B20) node not properly defined."
#endif
#endif

int temperature_measurement_init(void);
int temperature_measurement_read(void);
float temperature_measurement_last_value(void);

#endif /* _TEMPERATURE_MEASUREMENT_H */
