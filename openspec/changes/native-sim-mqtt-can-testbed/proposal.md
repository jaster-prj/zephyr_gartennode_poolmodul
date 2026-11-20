# Proposal

## Why

The pool module has a native-sim configuration, but pH and ORP ADC inputs cannot currently be changed at runtime. That prevents repeatable integration tests that configure calibration through CANopen, apply known pool conditions, and verify the resulting measurements transmitted over CAN.

## What Changes

- Add native-sim pH and ORP sensor models backed by Zephyr's ADC emulator, accepting pH values from 0–14 and ORP values in millivolts.
- Add a transport-only companion that relays values between MQTT and a local simulator socket; physical sensor modeling and analog conversion stay in the simulator.
- Add Docker-based native-sim test setup with configurable MQTT and UDP-CAN connectivity.
- Add end-to-end tests that configure calibration through CANopen, inject MQTT sensor values, and verify CAN measurement output.

## Capabilities

### New Capabilities

- `native-sim-pool-test`: Runtime pool sensor emulation and repeatable MQTT-to-CAN integration testing for the native-sim pool module.

### Modified Capabilities

None.

## Impact

- Zephyr native-sim configuration, devicetree overlays, pH/ORP measurement code, and simulator-specific sensor support.
- New MQTT/socket companion, Dockerfile and Compose setup, and CANopen test/observer tooling.
- Hardware-target ADC behavior remains unchanged.
- Calibration is configured through the existing CANopen object dictionary, including pH calibration objects `0x2106`/`0x2107` and ORP offset `0x2111`; the companion does not duplicate calibration or sensor conversion.
