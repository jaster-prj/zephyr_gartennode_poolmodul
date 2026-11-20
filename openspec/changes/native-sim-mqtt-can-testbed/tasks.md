# Tasks

## 1. Validate native-sim interfaces

- [x] 1.1 Build the current pool application for `native_sim` in the Zephyr workspace bundled in the persistent development container, and record the verified command and required dependencies in `docs/native-sim-test.md`.
- [x] 1.2 Create a minimal native-sim-to-host local socket proof of concept and verify bidirectional message exchange with a small host process; select and document the supported IPC framing.
- [x] 1.3 Verify Zephyr `adc_emul` can drive both pH and ORP channels at runtime in the current workspace version, with a focused native-sim test demonstrating that `adc_read()` observes updates.
- [x] 1.4 Verify how the UDP-CAN driver receives peer address and port configuration, then demonstrate two native-sim processes and an observer can exchange CAN frames using unique endpoints.

## 2. Implement native-sim pool sensor models

- [x] 2.1 Configure native-sim pH and ORP channels to use the Zephyr ADC emulator and verify the existing hardware-target devicetree and ADC paths are unchanged.
- [x] 2.2 Implement a runtime pH sensor model for 0–14 pH with explicit configurable sensor gain/offset, and add tests for range handling, voltage output, and ADC quantization.
- [x] 2.3 Implement a runtime ORP sensor model for millivolt inputs with explicit configurable sensor gain/offset, and add tests for supported range handling, voltage output, and ADC quantization.
- [x] 2.4 Connect the native-sim input receiver to both sensor models and verify valid updates affect subsequent ADC reads while invalid updates preserve the last valid input.

## 3. Implement the MQTT/socket companion

- [x] 3.1 Define module-scoped input and state topic configuration plus a versioned local socket frame carrying topic identity and opaque payload bytes; verify framing round-trips payloads unchanged.
- [x] 3.2 Implement MQTT subscription and forwarding for pH and ORP input topics without interpreting or transforming values, and add tests for connection loss, retained inputs, malformed messages, and reconnect behavior.
- [x] 3.3 Implement forwarding of simulator measurement/actor events to the corresponding MQTT state topics without recalculation, and verify payload preservation with companion tests.
- [x] 3.4 Document MQTT topic naming, broker/module configuration, payload ownership, QoS, and retained-message behavior in the test setup documentation.

## 4. Add CANopen-driven integration tests

- [x] 4.1 Implement test setup that writes pH calibration at `0x2106`/`0x2107` and ORP offset at `0x2111`; verify with a CANopen integration test.
- [x] 4.2 Implement expected-measurement calculations from the configured sensor transfer model, ADC resolution/averaging, and CANopen calibration; add unit tests for representative and boundary values.
- [x] 4.3 Add pH end-to-end coverage that configures calibration, publishes MQTT input, and verifies the resulting CAN measurement within a declared tolerance.
- [x] 4.4 Add ORP end-to-end coverage that configures calibration, publishes MQTT input, and verifies the resulting CAN measurement within a declared tolerance.
- [x] 4.5 Add actor/state relay coverage where supported and verify a simulator event is published unchanged to its module state topic.

## 5. Containerize and validate the testbed

- [x] 5.1 Add a repository-managed compatibility patch for the UDP-CAN module and a Dockerfile that applies it while building the native-sim application using the bundled Zephyr workspace/toolchain; verify the image build produces a runnable simulator.
- [x] 5.2 Add Docker Compose services for the simulator, MQTT companion, broker, and CAN observer/test runner with configurable MQTT identity and non-conflicting UDP-CAN endpoints; verify all services start and connect.
- [ ] 5.3 Document build, startup, configuration, calibration, and end-to-end test commands and verify each documented command works from the Zephyr workspace.
- [ ] 5.4 Run the full Compose MQTT-to-ADC-to-firmware-to-CAN test and a hardware-target compile check; verify the former passes and the latter remains unaffected.
