#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(socket_roundtrip, LOG_LEVEL_INF);

#define SIMULATOR_PORT 46001
#define HOST_PORT 46002
#define HEADER_SIZE 8
#define MAX_PACKET_SIZE 512

static ssize_t encode_frame(uint8_t *buffer, const char *topic, const char *payload)
{
    size_t topic_len = strlen(topic);
    size_t payload_len = strlen(payload);

    if (topic_len > UINT8_MAX || payload_len > UINT16_MAX ||
        HEADER_SIZE + topic_len + payload_len > MAX_PACKET_SIZE) {
        return -EINVAL;
    }

    memcpy(buffer, "GNS1", 4);
    buffer[4] = 1;
    buffer[5] = (uint8_t)topic_len;
    buffer[6] = (uint8_t)(payload_len >> 8);
    buffer[7] = (uint8_t)payload_len;
    memcpy(buffer + HEADER_SIZE, topic, topic_len);
    memcpy(buffer + HEADER_SIZE + topic_len, payload, payload_len);
    return (ssize_t)(HEADER_SIZE + topic_len + payload_len);
}

static bool frame_matches(const uint8_t *buffer, ssize_t size,
                          const char *topic, const char *payload)
{
    size_t topic_len = strlen(topic);
    size_t payload_len = strlen(payload);
    size_t declared_payload_len;

    if (size < HEADER_SIZE || memcmp(buffer, "GNS1", 4) != 0 || buffer[4] != 1 ||
        buffer[5] != topic_len) {
        return false;
    }
    declared_payload_len = ((size_t)buffer[6] << 8) | buffer[7];
    return declared_payload_len == payload_len &&
           (size_t)size == HEADER_SIZE + topic_len + payload_len &&
           memcmp(buffer + HEADER_SIZE, topic, topic_len) == 0 &&
           memcmp(buffer + HEADER_SIZE + topic_len, payload, payload_len) == 0;
}

int main(void)
{
    const char *input_topic = "garden/garden-1/modules/pool-1/input/ph";
    const char *state_topic = "garden/garden-1/modules/pool-1/state/sensor/ph";
    const char *payload = "7.25";
    uint8_t packet[MAX_PACKET_SIZE];
    struct sockaddr_in local = {0};
    struct sockaddr_in companion = {0};
    struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
    socklen_t address_len = sizeof(companion);
    ssize_t size;
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (fd < 0) {
        LOG_ERR("socket failed: %d", errno);
        return 1;
    }
    local.sin_family = AF_INET;
    local.sin_port = htons(SIMULATOR_PORT);
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) != 0 ||
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        LOG_ERR("socket setup failed: %d", errno);
        close(fd);
        return 1;
    }

    companion.sin_family = AF_INET;
    companion.sin_port = htons(HOST_PORT);
    companion.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    size = encode_frame(packet, input_topic, payload);
    if (size < 0 || sendto(fd, packet, size, 0,
                           (struct sockaddr *)&companion, sizeof(companion)) != size) {
        LOG_ERR("outbound frame send failed: %d", errno);
        close(fd);
        return 1;
    }

    size = recvfrom(fd, packet, sizeof(packet), 0,
                    (struct sockaddr *)&companion, &address_len);
    if (size < 0 || !frame_matches(packet, size, input_topic, payload)) {
        LOG_ERR("inbound frame or opaque payload mismatch");
        close(fd);
        return 1;
    }

    size = encode_frame(packet, state_topic, payload);
    if (size < 0 || sendto(fd, packet, size, 0,
                           (struct sockaddr *)&companion, address_len) != size) {
        LOG_ERR("state frame send failed: %d", errno);
        close(fd);
        return 1;
    }
    close(fd);
    LOG_INF("UDP framed socket roundtrip passed");
    return 0;
}
