# Design

## Context

See `proposal.md` for motivation and `specs/native-sim-pool-test/spec.md` for observable behavior. The application already selects pH and ORP ADC channels through devicetree and reads them through the normal measurement path. The native-sim configuration currently uses fixed/emulated inputs and the UDP-CAN overlay has fixed host and port values. Zephyr 4.4.2 in the development workspace provides `adc_emul` APIs for supplying runtime ADC voltages.

## Goals / Non-Goals

**Goals:**
- Keep the production sensor-reading and CANopen measurement path under test.
- Model physical pH/ORP values and analog gain in native-sim, independently of the firmware's CANopen calibration.
- Keep the MQTT companion transport-only, with configurable topic mappings and local IPC.
- Make calibration and expected CAN results explicit and reproducible in integration tests.

**Non-Goals:**
- Add MQTT or an HTTP server to the Zephyr firmware.
- Alter ADC behavior on the hardware target.
- Emulate all pool hardware or implement a general-purpose garden simulator.

## Decisions

### Use Zephyr ADC emulation behind native-sim sensor models

The native-sim sensor model owns the conversion from engineering inputs (pH in 0–14, ORP in mV) to the analog voltage presented to each ADC channel. Use the ADC emulator's runtime input functions so the existing ADC driver API, averaging, and measurement code remain exercised. Keep the sensor model compiled only for native-sim; hardware builds continue using their existing ADC configuration.

The sensor transfer curves and gain/offset parameters describe the emulated physical front-end. They are separate from CANopen calibration values. Tests choose both sets of values and calculate expected CAN measurements from the resulting ADC quantization and the calibration actually read back from the module.

### Keep the companion as an opaque MQTT/socket relay

The companion subscribes to module input topics and forwards their payload bytes and topic identity to the simulator over local IPC. It does not interpret engineering units, apply gain, convert to ADC counts, or apply CANopen calibration. Events emitted by the simulator may be forwarded to their corresponding MQTT state topics without recalculation.

Use module-scoped topics, for example `garden/{garden_id}/modules/{module_id}/input/ph`, `.../input/orp`, and corresponding `.../state/...` topics. Make broker address, garden/module identity, and topic root runtime configuration. Use QoS 1 for input and state delivery; retain the latest sensor input so a restarted simulator can receive the current environment.

Use host POSIX UDP sockets over container loopback for local IPC. A datagram uses an 8-byte header (`GNS1`, version byte `1`, topic-length byte, 16-bit big-endian payload length), followed by UTF-8 topic bytes and the opaque MQTT payload bytes. Reject invalid headers, lengths, topics, or datagrams above the configured maximum. UDP avoids enabling Zephyr's separate network stack/TAP solely for in-container IPC; QoS/replay semantics remain MQTT's responsibility.

### Set calibration through CANopen before sensor injection

The integration runner configures pH calibration at `0x2106` and `0x2107`, and ORP offset at `0x2111`, using CANopen SDO. It reads each value back and aborts the measurement phase on mismatch. MQTT values are injected only after calibration verification. Expected values are computed from the sensor-model transfer curve, ADC resolution/quantization and sample behavior, plus the verified firmware calibration—not by having the companion invert calibration.

### Containerize the actual workspace build and runtime

Build the native-sim application with the Zephyr workspace bundled in the development image and the CI toolchain pattern. The image contains the simulator and companion; Compose supplies a broker and CAN test observer on a shared test network. Give every simulated node a unique UDP-CAN endpoint and configure the driver's peer endpoint for the chosen virtual bus topology. Keep UDP-CAN settings separate from MQTT settings.

Manage the external UDP-CAN module's Zephyr 4.4.2 Kconfig compatibility fix as a small repository patch. The patch changes the module-relative Kconfig include to `rsource`, registers its devicetree root under `build.settings`, and removes initialization of a state-callback list not present in the bundled Zephyr CAN API. Apply it to the fetched dependency during the container build. This avoids maintaining a full driver fork while keeping builds reproducible.

## Risks / Trade-offs

- [Native-sim host IPC may not expose a suitable local socket API to Zephyr application code] → Make a minimal socket feasibility test the first implementation task; use the supported native-sim networking path if direct host IPC is unavailable.
- [Actual pH/ORP front-end gain and offset may differ from an initial model] → Keep transfer parameters explicit and test-configurable; document nominal values and validate them against hardware circuitry before claiming hardware-equivalent results.
- [ADC quantization and sample averaging can shift observed CAN values] → Calculate expected values through the quantized ADC path and use explicit, bounded test tolerances.
- [The UDP-CAN module may change its Kconfig layout upstream] → Apply the narrow compatibility patch only when all expected contexts match, and fail the image build if the patch no longer applies.
- [The UDP-CAN driver currently uses fixed devicetree endpoint values] → Determine whether runtime configuration is supported; otherwise generate per-instance overlays or build variants and assign non-conflicting ports.
- [Retained MQTT inputs can replay after restart] → Treat retained messages as the current environment state and make sensor updates idempotent.

## Migration Plan

This is an additive test-only environment. Build and run native-sim with Compose; no production firmware deployment or persistent data migration is required. Rollback consists of stopping the test stack and continuing to use the existing hardware build/configuration.
