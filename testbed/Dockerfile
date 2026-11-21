# The Zephyr devcontainer image bundles the Zephyr 4.4.2 workspace and host
# toolchain used by the verified native_sim build.
FROM ghcr.io/jaster-prj/devcontainer-zephyr:latest

USER root
WORKDIR /workspace

RUN /opt/venv/bin/west update

COPY patches/zephyr-native-linux-udp-can-compat.patch /tmp/udp-can-compat.patch
RUN test "$(git -C /workspace/zephyr-native-linux-udp-can rev-parse HEAD)" = \
      "7bc5d1ced6f49d7e348ddd090f7b62fa06a4354f" \
    && sed '/^diff --git a\/Kconfig /,/^ endmenu$/d' /tmp/udp-can-compat.patch \
      > /tmp/udp-can-driver-compat.patch \
    && git -C /workspace/zephyr-native-linux-udp-can apply /tmp/udp-can-driver-compat.patch

COPY . /workspace/application
RUN /opt/venv/bin/pip install --no-cache-dir paho-mqtt==2.1.0
RUN /opt/venv/bin/west build -b native_sim /workspace/application \
      --build-dir /workspace/application/build/native_sim \
      -- -DZEPHYR_EXTRA_MODULES=/workspace/zephyr-native-linux-udp-can \
         -DEXTRA_CONF_FILE=/workspace/application/boards/native_sim.conf \
         -DEXTRA_DTC_OVERLAY_FILE=/workspace/application/testbed/canopen/pool_can.overlay

ENV PYTHONPATH=/workspace/application/testbed/companion:/workspace/application/testbed/canopen
CMD ["/workspace/application/build/native_sim/zephyr/zephyr.exe"]
