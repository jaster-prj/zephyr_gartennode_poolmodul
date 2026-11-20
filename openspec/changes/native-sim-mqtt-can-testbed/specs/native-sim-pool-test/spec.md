# Spec Delta

## Purpose

This capability provides a repeatable native-sim test environment for the pool module, from runtime pH and ORP conditions through ADC sampling and CANopen measurement transmission.

## ADDED Requirements

### Requirement: Runtime pH sensor emulation
The native-sim pool module SHALL accept runtime pH input values in the range 0 through 14 and model the pH sensor and its analog gain path as an ADC input, without changing the hardware-target sensor path.

#### Scenario: Apply a valid pH condition
- **WHEN** a valid pH input is delivered to the running native-sim module
- **THEN** the emulated ADC input reflects the modeled pH sensor voltage and the existing firmware measurement path produces the corresponding pH reading

#### Scenario: Reject an out-of-range pH condition
- **WHEN** a pH input below 0 or above 14 is delivered
- **THEN** the simulator rejects the input and does not replace the last valid pH condition

### Requirement: Runtime ORP sensor emulation
The native-sim pool module SHALL accept runtime ORP input values in millivolts and model the ORP sensor and its analog gain path as an ADC input, without changing the hardware-target sensor path.

#### Scenario: Apply a valid ORP condition
- **WHEN** a valid ORP millivolt input is delivered to the running native-sim module
- **THEN** the emulated ADC input reflects the modeled ORP sensor voltage and the existing firmware measurement path produces the corresponding ORP reading

#### Scenario: Reject an invalid ORP condition
- **WHEN** an ORP input is malformed or cannot be represented by the configured sensor model and ADC input range
- **THEN** the simulator rejects the input and does not replace the last valid ORP condition

### Requirement: MQTT transport companion
The test setup SHALL provide a companion that relays sensor inputs between MQTT and the simulator over a local socket without applying sensor gain, ADC conversion, or CANopen calibration.

#### Scenario: Relay a sensor input
- **WHEN** a valid pH or ORP input message arrives on the configured module input topic
- **THEN** the companion forwards the input payload to the simulator over the local socket without altering its measurement value

#### Scenario: Relay simulator output
- **WHEN** the simulator emits a supported measurement or actor-state event over the local socket
- **THEN** the companion publishes that event on the corresponding configured MQTT state topic without recalculating the value

### Requirement: CANopen calibration in integration tests
The integration test setup SHALL configure pool-module calibration through CANopen before evaluating sensor-to-CAN measurements.

#### Scenario: Configure pH calibration
- **WHEN** a test begins its measurement phase
- **THEN** it writes the selected pH calibration points and values through object-dictionary entries `0x2106` and `0x2107` before injecting pH input

#### Scenario: Configure ORP calibration
- **WHEN** a test begins its measurement phase
- **THEN** it writes the selected ORP offset through object-dictionary entry `0x2111` before injecting ORP input

### Requirement: Verify measurement transmitted over CAN
The test setup SHALL verify that the running pool firmware transmits measurements derived from emulated ADC readings over CAN using its configured CANopen calibration.

#### Scenario: Verify pH measurement
- **WHEN** a test configures pH calibration, injects a valid pH condition, and observes the module's CAN measurement
- **THEN** the transmitted pH measurement matches the expected calibrated result within the test's declared tolerance

#### Scenario: Verify ORP measurement
- **WHEN** a test configures ORP calibration, injects a valid ORP condition, and observes the module's CAN measurement
- **THEN** the transmitted ORP measurement matches the expected calibrated result within the test's declared tolerance

### Requirement: Containerized native-sim test environment
The test setup SHALL provide a reproducible container environment for the native-sim pool module, MQTT transport companion, broker, and CAN test observer, with module-specific network configuration.

#### Scenario: Run an isolated module test
- **WHEN** the pool-module test environment is started with its configured MQTT identity and UDP-CAN endpoints
- **THEN** the simulator and companion connect to the configured broker and CAN peer without requiring changes to the hardware-target build
