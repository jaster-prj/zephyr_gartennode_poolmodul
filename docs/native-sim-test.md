# Native-sim pool test development

## Build prerequisites

Use the persistent `ghcr.io/jaster-prj/devcontainer-zephyr:latest` development container. Its `/workspace` contains the Zephyr workspace; do not mount a second workspace over it. The pool application is mounted at `/workspace/application`.

The current native-sim build uses Zephyr 4.5, the bundled CANopenNodeZephyr and
board modules, and `zephyr-native-linux-udp-can` at
`/workspace/zephyr-native-linux-udp-can`. The UDP-CAN module owns its wire
format, idle-bus RX scheduling, and devicetree binding discovery; no firmware
repository patches the dependency at build time.

CANopen startup/host-clock handling and initial/retried pump actor feedback
are implemented directly in `src/main.c` and `src/waterpump.c`, guarded for
native-sim. No image build rewrites these application sources. Hardware startup
and GPIO control remain on their existing non-native paths.

`src/native_sim_ds18b20.c` is simulator-only and is added to the application
only for `CONFIG_BOARD_NATIVE_SIM`; CMake excludes it from hardware builds. Its
non-simulator stubs remain self-contained for configurations that compile the
file directly, with the required errno and Zephyr utility headers available
outside the simulator guard.

From `/workspace`, the verified build command is:

```sh
/opt/venv/bin/west build -b native_sim application \
  --build-dir application/build/native_sim -- \
  -DZEPHYR_EXTRA_MODULES=/workspace/zephyr-native-linux-udp-can \
  -DEXTRA_CONF_FILE=boards/native_sim.conf
```

The application-specific native-sim configuration is `boards/native_sim.conf`.

For the Virtual Garden UDP-CAN endpoint (local port 46110) and the standalone
startup/actor regression, add
`-DEXTRA_DTC_OVERLAY_FILE=/workspace/application/testbed/canopen/pool_can.overlay`
to that build command, then run:

```sh
python3 application/tests/native_sim_pool/run_startup_actor.py \
  application/build/native_sim/zephyr/zephyr.exe
```

The test uses its own UDP gateway and companion sockets and verifies boot-up
ordering, heartbeat progression without incoming CAN traffic, initial OFF
feedback after the input handshake, and sampling-pump ON/OFF reports following
SDO commands. It does not connect to MQTT or physical hardware.

## Local simulator/companion IPC

The simulator uses host POSIX UDP sockets on container loopback. The companion and simulator exchange one UDP datagram per event. Frames use the eight-byte header `GNS1 | version:u8 | topic_length:u8 | payload_length:u16be`, followed by the UTF-8 topic and unchanged MQTT payload bytes. The socket roundtrip proof of concept is built and exercised with:

```sh
/opt/venv/bin/west build -b native_sim application/tests/native_sim_socket \
  --build-dir application/build/socket_roundtrip
/opt/venv/bin/python application/tests/native_sim_socket/run_roundtrip.py \
  application/build/socket_roundtrip/zephyr/zephyr.exe
```

## ADC emulator smoke test

Build the two-channel pH/ORP ADC emulator test and run its host-side assertion helper:

```sh
/opt/venv/bin/west build -b native_sim application/tests/native_sim_adc \
  --build-dir application/build/adc_emul
/opt/venv/bin/python application/tests/native_sim_adc/run_test.py \
  application/build/adc_emul/zephyr/zephyr.exe
```

## UDP-CAN transport proof

The `native-linux-udp-can` driver takes gateway `host`/`port` and socket `local-port` from its devicetree node. The focused test builds two nodes with distinct local ports and gateway destinations, then runs a UDP gateway/observer that forwards each wire frame to the other node. It observes test CAN IDs `0x109`, `0x10A`, `0x209`, and `0x20A`. ACKs are sent from each node's receive callback after it receives the peer hello.

Build both node variants in separate build directories and run the observer:

```sh
/opt/venv/bin/west build -b native_sim application/tests/native_sim_can \
  --build-dir application/build/tests_can_node9 -- \
  -DZEPHYR_EXTRA_MODULES=/workspace/zephyr-native-linux-udp-can \
  -DEXTRA_CONF_FILE=/workspace/application/tests/native_sim_can/node_9.conf \
  -DEXTRA_DTC_OVERLAY_FILE=/workspace/application/tests/native_sim_can/node_9.overlay
/opt/venv/bin/west build -b native_sim application/tests/native_sim_can \
  --build-dir application/build/tests_can_node10 -- \
  -DZEPHYR_EXTRA_MODULES=/workspace/zephyr-native-linux-udp-can \
  -DEXTRA_CONF_FILE=/workspace/application/tests/native_sim_can/node_10.conf \
  -DEXTRA_DTC_OVERLAY_FILE=/workspace/application/tests/native_sim_can/node_10.overlay
/opt/venv/bin/python application/tests/native_sim_can/run_can_bus.py \
  application/build/tests_can_node9/zephyr/zephyr.exe \
  application/build/tests_can_node10/zephyr/zephyr.exe
```

## ADC paths and target isolation

The native-sim overlay selects ADC channels 0 and 1 for pH and ORP and configures their 12-bit channel parameters. The native_sim board supplies the `zephyr,adc-emul` controller (`adc0`); its generated devicetree confirms that compatible and both channels. The hardware target remains on `adc1` channels 7 and 8 from `boards/gartennode.overlay`. Sensor read logic continues to use the existing `adc_dt_spec`/`adc_read()` path for both builds; simulator-only transfer models are separately guarded and do not replace hardware ADC setup.

## Test sensor transfer models

The native-sim-only model maps pH to `offset + pH * gain` mV and ORP to `offset - ORP_mV * gain_milli / 1000` mV to match the inverting ORP front-end. Defaults are pH 150 mV/pH with 600 mV offset and ORP unity gain with a 1320 mV zero point. Override with `CONFIG_APP_NATIVE_SIM_PH_GAIN_MV_PER_PH`, `CONFIG_APP_NATIVE_SIM_PH_OFFSET_MV`, `CONFIG_APP_NATIVE_SIM_ORP_GAIN_PER_MV`, and `CONFIG_APP_NATIVE_SIM_ORP_OFFSET_MV`. These are configurable test defaults, not validated hardware-front-end specifications. Inputs outside the model's domain or outputs outside the ADC reference are rejected. The model tests cover range boundaries, voltage mapping, invalid/unrepresentable inputs, and ADC quantization through the actual emulator read path.

Re-run them with the ADC emulator command above; successful output includes `ADC emulator runtime updates passed for pH and ORP`.

The simulator listens on `CONFIG_APP_NATIVE_SIM_IPC_PORT` (default UDP 46003) and polls it from the normal firmware loop. Input topics ending in `/input/ph` carry decimal pH text; topics ending in `/input/orp` carry signed integer millivolts. The receiver validates frame version/length, rejects malformed or unrepresentable input, and does not change the previous ADC condition on rejection. The ADC test runner sends valid updates followed by out-of-range and wrong-version frames and checks the resulting quantized ADC readings.

## MQTT companion behavior and topic configuration

The transport-only companion is in `testbed/companion`. Set `GARDEN_ID` and `MODULE_ID` to construct topics under `garden/{garden_id}/modules/{module_id}`. Input topics are `input/ph` and `input/orp`; measurement state topics are `state/measurement/ph` and `state/measurement/orp`; actor state events use `state/actor/{actor}`. The companion subscribes to both input topics at QoS 1 and forwards the MQTT payload bytes unchanged in the version-1 `GNS1` local UDP frame. It does not parse units or sensor values. The simulator interprets those inputs and owns transfer modeling and CANopen calibration.

Configure the broker and sockets with `MQTT_HOST` (default `mosquitto`), `MQTT_PORT` (1883), `MQTT_CLIENT_ID`, `SIMULATOR_HOST` (127.0.0.1), `SIMULATOR_IPC_PORT` (46003), and `COMPANION_IPC_PORT` (46004). `paho-mqtt` reconnects automatically; a clean session re-subscribes after reconnect, so the broker replays retained sensor inputs. Firmware measurements are published at QoS 1 without retain. Actor events are retained as last-known physical output state; they are not proof of current connectivity. The firmware retries its initial pump state until a valid input establishes the module topic root.

Run companion protocol and relay unit tests with:

```sh
python3 -m unittest discover -s testbed/companion -v
```

For broker-backed retained-input and reconnect verification, start a Mosquitto broker and run:

```sh
RUN_MQTT_INTEGRATION=1 MQTT_HOST=127.0.0.1 MQTT_PORT=1883 \
  /opt/venv/bin/python -m unittest discover \
  -s application/testbed/companion -p test_mqtt_broker.py -v
```

This test publishes a retained pH input before the companion subscribes, verifies broker replay and replay after reconnect, and confirms malformed ORP bytes are forwarded unchanged.

## Expected CANopen measurements

`testbed/canopen/expected_measurements.py` models the native-sim affine sensor transfer, ADC emulator quantization (`floor(mV * ((1 << resolution) - 1) / Vref)`), constant-channel averaging, and the checked-in pH/ORP conversion and clamp formulas. Since the ADC emulator produces the same code for each sample in a channel, the firmware's average/trimmed-average returns that code for any configured sample count. Run the calculation and SDO-writer tests with:

```sh
python3 -m unittest discover -s testbed/canopen -v
```

The ORP measurement converts ADC codes to millivolts using the configured ADC resolution, then applies the 1.32 V inverting-front-end zero point and CANopen offset. This avoids the former fixed-1024 divisor and unsigned-subtraction error on the 12-bit native-sim ADC.

## Container build and Compose testbed

Build the runnable simulator image from the repository root (no Zephyr workspace bind mount is used; the base image supplies the verified bundled workspace):

```sh
docker build -f testbed/Dockerfile \
  --build-context native-udp-src=../zephyr-native-linux-udp-can \
  -t pool-native-sim-testbed:local .
docker build -f testbed/companion/Dockerfile -t testbed-companion:latest .
```

The dependency fixes are part of `zephyr-native-linux-udp-can` itself. The
firmware is built unchanged from this repository. Virtual Garden uses this same
`testbed/Dockerfile`; it does not patch the UDP-CAN source.
Start broker, companion, simulator, and CAN observer with the locally built images:

```sh
docker compose -f testbed/compose.yaml --profile test up -d --no-build --pull never
```

Override `GARDEN_ID`, `MODULE_ID`, `MQTT_CLIENT_ID`, and `MQTT_PUBLISHED_PORT` in the environment. The simulator shares the companion network namespace so its loopback UDP IPC and CAN gateway endpoints remain local, while the companion reaches the broker by Compose DNS. Run the CANopen expected-measurement and SDO-writer unit tests using the optional test profile:

```sh
docker compose -f testbed/compose.yaml --profile test run --rm test-runner
```

Send the configured calibration writes through the running CAN observer with:

```sh
docker compose -f testbed/compose.yaml --profile test run --rm --no-deps \
  --entrypoint /opt/venv/bin/python test-runner \
  /workspace/application/testbed/canopen/run_calibration.py
```

Run the retained MQTT/reconnect integration test against the running Compose broker with:

```sh
docker exec -e RUN_MQTT_INTEGRATION=1 testbed-companion-1 \
  python -m unittest discover -s /app -p test_mqtt_broker.py -v
```

Stop the testbed with `docker compose -f testbed/compose.yaml down`. The Zephyr development container is independent and remains running.

Hardware-target isolation check (built in the persistent development container):

```sh
/opt/venv/bin/west build -b gartennode application --build-dir application/build/gartennode
```

The Zephyr 4.4.2 `gartennode/stm32l431xx` build completed successfully with the existing hardware overlay and ADC path.
