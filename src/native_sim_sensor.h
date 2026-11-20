#ifndef APP_NATIVE_SIM_SENSOR_H_
#define APP_NATIVE_SIM_SENSOR_H_

#include <stddef.h>
#include <stdint.h>

/* Pure transfer helpers are exposed for deterministic model tests. */
int native_sim_ph_voltage_mv(double ph, int32_t gain_mv_per_ph, int32_t offset_mv,
			     uint32_t vref_mv, uint32_t *voltage_mv);
int native_sim_orp_voltage_mv(int32_t orp_mv, int32_t gain_milli, int32_t offset_mv,
			      uint32_t vref_mv, uint32_t *voltage_mv);

/* Native-sim runtime ADC model API. */
int native_sim_sensor_init(void);
int native_sim_sensor_set_ph(double ph);
int native_sim_sensor_set_orp(int32_t orp_mv);
uint32_t native_sim_sensor_ipc_packet_count(void);
int native_sim_sensor_process(void);
int native_sim_sensor_publish_state(const char *suffix, const uint8_t *payload, size_t payload_len);

#endif /* APP_NATIVE_SIM_SENSOR_H_ */
