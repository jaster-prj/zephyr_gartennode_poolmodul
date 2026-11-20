#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define NODE_ID CONFIG_TEST_CAN_NODE_ID

static void on_frame(const struct device *dev, struct can_frame *frame, void *user_data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);
    if (frame->id == (0x100U + (NODE_ID == 9 ? 10U : 9U)) && frame->dlc == 1) {
        struct can_frame ack = {
            .id = 0x200U + NODE_ID,
            .dlc = 1,
            .data = {NODE_ID},
        };

        printk("received peer frame 0x%x; sending ACK 0x%x\n", frame->id, ack.id);
        if (can_send(dev, &ack, K_MSEC(100), NULL, NULL) != 0) {
            printk("CAN ACK send failed\n");
        }
    }
}

int main(void)
{
    const struct device *can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
    const struct can_filter filter = {.id = 0, .mask = 0};
    struct can_frame hello = {.id = 0x100U + NODE_ID, .dlc = 1};
    int ret;

    if (!device_is_ready(can_dev)) {
        printk("CAN device not ready\n");
        return 1;
    }
    ret = can_add_rx_filter(can_dev, on_frame, NULL, &filter);
    if (ret < 0) {
        printk("CAN RX filter failed: %d\n", ret);
        return 1;
    }
    ret = can_start(can_dev);
    if (ret != 0) {
        printk("CAN start failed: %d\n", ret);
        return 1;
    }
    hello.data[0] = NODE_ID;
    while (true) {
        ret = can_send(can_dev, &hello, K_MSEC(100), NULL, NULL);
        if (ret != 0) {
            printk("CAN hello send failed: %d\n", ret);
            return 1;
        }
        k_sleep(K_MSEC(50));
    }
    return 0;
}
