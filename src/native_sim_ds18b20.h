#ifndef APP_NATIVE_SIM_DS18B20_H_
#define APP_NATIVE_SIM_DS18B20_H_

#include <stdint.h>

int native_sim_ds18b20_init(void);
void native_sim_ds18b20_set_temperature(double temperature_c);
int native_sim_ds18b20_set_rom(uint64_t rom_id);

#endif
