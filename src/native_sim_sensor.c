#include "native_sim_sensor.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#if defined(CONFIG_BOARD_NATIVE_SIM)
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define NATIVE_SIM_ADC_VREF_MV 3300U
#define NATIVE_SIM_PH_MIN 0.0
#define NATIVE_SIM_PH_MAX 14.0
#define NATIVE_SIM_ORP_MIN_MV (-1000)
#define NATIVE_SIM_ORP_MAX_MV 1000
#define NATIVE_SIM_IPC_MAX_PACKET_SIZE 512
#define NATIVE_SIM_IPC_HEADER_SIZE 8
#define NATIVE_SIM_IPC_TOPIC_SUFFIX_PH "/input/ph"
#define NATIVE_SIM_IPC_TOPIC_SUFFIX_ORP "/input/orp"
#define NATIVE_SIM_IPC_TOPIC_ROOT_MAX 220

int native_sim_ph_voltage_mv(double ph, int32_t gain_mv_per_ph, int32_t offset_mv,
			     uint32_t vref_mv, uint32_t *voltage_mv)
{
	double result;

	if (voltage_mv == NULL || !(ph >= NATIVE_SIM_PH_MIN && ph <= NATIVE_SIM_PH_MAX) ||
	    gain_mv_per_ph <= 0 || offset_mv < 0 || vref_mv == 0U) {
		return -EINVAL;
	}
	result = (ph * gain_mv_per_ph) + offset_mv;
	if (result < 0.0 || result > vref_mv) {
		return -ERANGE;
	}
	*voltage_mv = (uint32_t)(result + 0.5);
	return 0;
}

int native_sim_orp_voltage_mv(int32_t orp_mv, int32_t gain_milli, int32_t offset_mv,
			      uint32_t vref_mv, uint32_t *voltage_mv)
{
	int64_t scaled;
	int64_t result;

	if (voltage_mv == NULL || orp_mv < NATIVE_SIM_ORP_MIN_MV ||
	    orp_mv > NATIVE_SIM_ORP_MAX_MV || gain_milli <= 0 || offset_mv < 0 ||
	    vref_mv == 0U) {
		return -EINVAL;
	}
	scaled = (int64_t)orp_mv * gain_milli;
	result = (int64_t)offset_mv - (scaled >= 0 ? scaled + 500 : scaled - 500) / 1000;
	if (result < 0 || result > vref_mv) {
		return -ERANGE;
	}
	*voltage_mv = (uint32_t)result;
	return 0;
}

#if defined(CONFIG_BOARD_NATIVE_SIM)
static const struct adc_dt_spec ph_adc = ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), ph_sensor);
static const struct adc_dt_spec orp_adc = ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), orp_sensor);
static int ipc_fd = -1;
static volatile uint32_t ipc_packet_count;
static char topic_root[NATIVE_SIM_IPC_TOPIC_ROOT_MAX + 1];

static void remember_topic_root(const char *topic, size_t topic_len, size_t suffix_len)
{
	size_t root_len = topic_len - suffix_len;

	if (root_len == 0 || root_len > NATIVE_SIM_IPC_TOPIC_ROOT_MAX) {
		return;
	}
	memcpy(topic_root, topic, root_len);
	topic_root[root_len] = '\0';
}

static bool topic_ends_with(const char *topic, size_t topic_len, const char *suffix)
{
	size_t suffix_len = strlen(suffix);

	return topic_len > suffix_len &&
		memcmp(topic + topic_len - suffix_len, suffix, suffix_len) == 0;
}

static bool parse_ph_payload(const char *payload, double *ph)
{
	bool dot_seen = false;
	bool digit_seen = false;
	uint32_t whole = 0;
	uint32_t fraction = 0;
	uint32_t scale = 1;

	for (const char *cur = payload; *cur != '\0'; cur++) {
		if (*cur == '.' && !dot_seen) {
			dot_seen = true;
			continue;
		}
		if (*cur < '0' || *cur > '9') {
			return false;
		}
		digit_seen = true;
		if (!dot_seen) {
			whole = whole * 10U + (uint32_t)(*cur - '0');
			if (whole > 14U) return false;
		} else {
			if (scale >= 1000000U) return false;
			scale *= 10U;
			fraction = fraction * 10U + (uint32_t)(*cur - '0');
		}
	}
	if (!digit_seen) return false;
	*ph = (double)whole + ((double)fraction / scale);
	return true;
}

static bool parse_orp_payload(const char *payload, int32_t *orp_mv)
{
	bool negative = false;
	uint32_t value = 0;
	const char *cur = payload;

	if (*cur == '-' || *cur == '+') {
		negative = *cur == '-';
		cur++;
	}
	if (*cur == '\0') return false;
	for (; *cur != '\0'; cur++) {
		if (*cur < '0' || *cur > '9') return false;
		uint32_t digit = (uint32_t)(*cur - '0');
		if (value > (1000U - digit) / 10U) return false;
		value = value * 10U + digit;
	}
	*orp_mv = negative ? -(int32_t)value : (int32_t)value;
	return true;
}

static void process_ipc_packet(uint8_t *packet, size_t packet_len)
{
	uint8_t topic_len;
	size_t payload_len;
	char *topic;
	char *payload;

	if (packet_len < NATIVE_SIM_IPC_HEADER_SIZE ||
	    memcmp(packet, "GNS1", 4) != 0 || packet[4] != 1) {
		return;
	}

	topic_len = packet[5];
	payload_len = ((size_t)packet[6] << 8) | packet[7];
	if (topic_len == 0 || payload_len == 0 ||
	    packet_len != NATIVE_SIM_IPC_HEADER_SIZE + topic_len + payload_len) {
		return;
	}

	topic = (char *)&packet[NATIVE_SIM_IPC_HEADER_SIZE];
	payload = topic + topic_len;
	if (memchr(topic, '\0', topic_len) != NULL || memchr(payload, '\0', payload_len) != NULL) {
		return;
	}
	payload[payload_len] = '\0';

	if (topic_ends_with(topic, topic_len, NATIVE_SIM_IPC_TOPIC_SUFFIX_PH)) {
		double ph;

		if (parse_ph_payload(payload, &ph) && native_sim_sensor_set_ph(ph) == 0) {
			remember_topic_root(topic, topic_len, strlen(NATIVE_SIM_IPC_TOPIC_SUFFIX_PH));
		}
	} else if (topic_ends_with(topic, topic_len, NATIVE_SIM_IPC_TOPIC_SUFFIX_ORP)) {
		int32_t orp;

		if (parse_orp_payload(payload, &orp) && native_sim_sensor_set_orp(orp) == 0) {
			remember_topic_root(topic, topic_len, strlen(NATIVE_SIM_IPC_TOPIC_SUFFIX_ORP));
		}
	}
}

int native_sim_sensor_init(void)
{
	struct sockaddr_in local = {0};
	int ret;

	if (!adc_is_ready_dt(&ph_adc) || !adc_is_ready_dt(&orp_adc)) {
		return -ENODEV;
	}
	ret = native_sim_sensor_set_ph(7.0);
	if (ret != 0) {
		return ret;
	}
	ret = native_sim_sensor_set_orp(0);
	if (ret != 0) {
		return ret;
	}

	ipc_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (ipc_fd < 0) {
		return -errno;
	}
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	local.sin_port = htons(CONFIG_APP_NATIVE_SIM_IPC_PORT);
	if (bind(ipc_fd, (struct sockaddr *)&local, sizeof(local)) != 0) {
		ret = -errno;
		close(ipc_fd);
		ipc_fd = -1;
		return ret;
	}

	return 0;
}

int native_sim_sensor_set_ph(double ph)
{
	uint32_t voltage_mv;
	int ret = native_sim_ph_voltage_mv(ph, CONFIG_APP_NATIVE_SIM_PH_GAIN_MV_PER_PH,
		CONFIG_APP_NATIVE_SIM_PH_OFFSET_MV, NATIVE_SIM_ADC_VREF_MV, &voltage_mv);

	if (ret != 0) {
		return ret;
	}
	ret = adc_emul_const_value_set(ph_adc.dev, ph_adc.channel_id, voltage_mv);
	return ret;
}

uint32_t native_sim_sensor_ipc_packet_count(void)
{
#if defined(CONFIG_BOARD_NATIVE_SIM)
	return ipc_packet_count;
#else
	return 0;
#endif
}

int native_sim_sensor_publish_state(const char *suffix, const uint8_t *payload, size_t payload_len)
{
#if defined(CONFIG_BOARD_NATIVE_SIM)
	uint8_t frame[NATIVE_SIM_IPC_MAX_PACKET_SIZE];
	struct sockaddr_in companion = {0};
	size_t root_len = strlen(topic_root);
	size_t suffix_len;
	size_t topic_len;
	size_t frame_len;

	if (suffix == NULL || (payload == NULL && payload_len != 0) || root_len == 0) {
		return -EINVAL;
	}
	suffix_len = strlen(suffix);
	topic_len = root_len + suffix_len;
	frame_len = NATIVE_SIM_IPC_HEADER_SIZE + topic_len + payload_len;
	if (suffix[0] != '/' || topic_len > UINT8_MAX || payload_len > UINT16_MAX ||
	    frame_len > sizeof(frame)) {
		return -EMSGSIZE;
	}
	memcpy(frame, "GNS1", 4);
	frame[4] = 1;
	frame[5] = (uint8_t)topic_len;
	frame[6] = (uint8_t)(payload_len >> 8);
	frame[7] = (uint8_t)payload_len;
	memcpy(frame + NATIVE_SIM_IPC_HEADER_SIZE, topic_root, root_len);
	memcpy(frame + NATIVE_SIM_IPC_HEADER_SIZE + root_len, suffix, suffix_len);
	if (payload_len != 0) {
		memcpy(frame + NATIVE_SIM_IPC_HEADER_SIZE + topic_len, payload, payload_len);
	}
	companion.sin_family = AF_INET;
	companion.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	companion.sin_port = htons(CONFIG_APP_NATIVE_SIM_COMPANION_IPC_PORT);
	return sendto(ipc_fd, frame, frame_len, 0, (struct sockaddr *)&companion,
		      sizeof(companion)) == (ssize_t)frame_len ? 0 : -EIO;
#else
	ARG_UNUSED(suffix);
	ARG_UNUSED(payload);
	ARG_UNUSED(payload_len);
	return -ENOTSUP;
#endif
}

int native_sim_sensor_process(void)
{
#if defined(CONFIG_BOARD_NATIVE_SIM)
	uint8_t packet[NATIVE_SIM_IPC_MAX_PACKET_SIZE + 1];
	int processed = 0;

	while (true) {
		ssize_t received = recv(ipc_fd, packet, sizeof(packet), MSG_DONTWAIT);

		if (received < 0) {
			return errno == EAGAIN || errno == EWOULDBLOCK ? processed : -errno;
		}
		if (received > 0 && received <= NATIVE_SIM_IPC_MAX_PACKET_SIZE) {
			process_ipc_packet(packet, (size_t)received);
			ipc_packet_count++;
			processed++;
		}
	}
#else
	return -ENOTSUP;
#endif
}

int native_sim_sensor_set_orp(int32_t orp_mv)
{
	uint32_t voltage_mv;
	int ret = native_sim_orp_voltage_mv(orp_mv, CONFIG_APP_NATIVE_SIM_ORP_GAIN_PER_MV,
		CONFIG_APP_NATIVE_SIM_ORP_OFFSET_MV, NATIVE_SIM_ADC_VREF_MV, &voltage_mv);

	if (ret != 0) {
		return ret;
	}
	ret = adc_emul_const_value_set(orp_adc.dev, orp_adc.channel_id, voltage_mv);
	return ret;
}
#else
int native_sim_sensor_init(void) { return -ENOTSUP; }
int native_sim_sensor_set_ph(double ph) { ARG_UNUSED(ph); return -ENOTSUP; }
int native_sim_sensor_set_orp(int32_t orp_mv) { ARG_UNUSED(orp_mv); return -ENOTSUP; }
uint32_t native_sim_sensor_ipc_packet_count(void) { return 0; }
int native_sim_sensor_publish_state(const char *suffix, const uint8_t *payload, size_t payload_len) { ARG_UNUSED(suffix); ARG_UNUSED(payload); ARG_UNUSED(payload_len); return -ENOTSUP; }
int native_sim_sensor_process(void) { return -ENOTSUP; }
#endif
