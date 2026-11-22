# Build the repository's native application unchanged in the bundled workspace.
FROM ghcr.io/jaster-prj/devcontainer-zephyr:latest

USER root
WORKDIR /workspace

COPY . /workspace/application
COPY --from=native-udp-src . /workspace/zephyr-native-linux-udp-can
# The same image also runs the repository's CAN observer/integration tests.
RUN /opt/venv/bin/pip install --no-cache-dir -r /workspace/application/testbed/companion/requirements.txt
RUN /opt/venv/bin/west build -b native_sim /workspace/application \
      --build-dir /workspace/application/build/native_sim \
      -- -DZEPHYR_EXTRA_MODULES=/workspace/zephyr-native-linux-udp-can \
          -DEXTRA_CONF_FILE=/workspace/application/boards/native_sim.conf \
          -DEXTRA_DTC_OVERLAY_FILE=/workspace/application/testbed/canopen/pool_can.overlay

ENV PYTHONPATH=/workspace/application/testbed/companion:/workspace/application/testbed/canopen
CMD ["/workspace/application/build/native_sim/zephyr/zephyr.exe"]
