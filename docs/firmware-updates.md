# Pool module firmware updates

These fixes are ported from the cistern module. Cistern hardware tests do not
establish that the pool module has passed a hardware update test.
Pool node ID remains **9** (`0x09`); sensors, pump control and application
watchdog settings are unchanged.

## Build and release

Use the Gartennode west workspace and matching private signing key at
`bootloader/mcuboot/gartennode.pem`. Never commit or publish the key.
From the workspace root:

```sh
west build -b gartennode application -d build/pool
python application/scripts/check_firmware.py \
  build/pool/zephyr/zephyr.signed.bin build/pool/zephyr/.config
python bootloader/mcuboot/scripts/imgtool.py verify \
  --key bootloader/mcuboot/gartennode.pem build/pool/zephyr/zephyr.signed.bin
python application/scripts/prepare_eds.py \
  build/pool/zephyr/.config application/objdict/objdict.eds objdict.eds
python -m unittest discover -s application/scripts -p 'test_*.py'
```

Publish Zephyr's `zephyr.signed.bin`, not an image re-signed with a hard-coded
header size. Zephyr 4.5 uses a `0x400` STM32 header. The default signed version
is `0.1.0`; override `CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION` for each new release.
The running `0x100A` string and release EDS use that same version.

Both application and MCUboot use **swap using scratch**:

| Region | Address/offset | Size |
| --- | --- | --- |
| MCUboot, internal | `0x08000000` | 64 KiB |
| Primary, internal | `0x08010000` | 192 KiB |
| Secondary, external | `0` | 192 KiB |
| Scratch, external | `0x30000` | 192 KiB |

Build matching MCUboot separately:

```sh
west build -b gartennode bootloader/mcuboot/boot/zephyr \
  -d build/pool-boot \
  -- -DEXTRA_CONF_FILE="$PWD/application/bootloader.conf"
```

Verify the installed bootloader's layout and signing key before deployment.
The supplied bootloader keeps its existing watchdog-disabled policy; the pool
application retains `CONFIG_WATCHDOG=y` and `CONFIG_TASK_WDT=y`. This port does
not change watchdog initialization, feeding or channel registration.

## CAN protocol

Use node `0x9` and the uploaded image's version:

```sh
curl --cacert CA.pem \
  -H 'Content-Type: application/octet-stream' \
  --data-binary @build/pool/zephyr/zephyr.signed.bin \
  'https://YOUR_GATEWAY/canopenrest/api/v1/flash?node=0x9&version=1.3.1'
```

Poll `GET /canopenrest/api/v1/flash?id=<returned-id>` to completion.
Require reboot, matching `0x100A`, and explicit confirmation—not just successful
data reception.

STOP/CLEAR/START retain upstream PRE-OPERATIONAL restrictions. CLEAR erases the
secondary slot; reception-time stream-flash erasing is disabled to avoid
erasing it again during SDO handling.

The adapter allows explicit `0x1F51:1 = 0x80` confirmation while OPERATIONAL,
because the application enters that state before gateway verification.
It never automatically confirms an image; MCUboot rollback remains available
for unconfirmed test images.

The cistern's swap/startup took about 16.8 seconds, longer than the gateway's
fixed ten-second initial wait. Allow adequate reboot/verification time; pool
timing has not been measured.

## SWD recovery

Back up internal flash before recovery. Use a **full padded BIN**, not sparse
HEX: sparse programming can leave old swap-status bytes in a primary trailer
page and cause MCUboot to interpret an invalid interrupted swap.

Generate a confirmed baseline separately:

```sh
west build -b gartennode application -d build/pool-recovery \
  -- -DCONFIG_MCUBOOT_GENERATE_CONFIRMED_IMAGE=y
```

Verify layout/signature and exact `0x30000`-byte length before programming
`zephyr.signed.confirmed.bin` at `0x08010000`. Resolve pending external
swap state first; do not resume a pending swap against an unrelated replacement
primary. Avoid mass erases of the bootloader or external settings.
Do not use cistern firmware as a pool recovery image.

## Optional diagnostics

Add `-DEXTRA_CONF_FILE=debug/update.conf` when building the application.
It records reset causes, download byte counts and callback latency, using
deferred RTT logs and pool-specific sensor log filters. Sensors remain enabled.
Read `pool_update_diagnostics` and RTT only after a transfer; live SWD reads or
halts can disturb timing.

`debug/bootloader.conf` is an optional second MCUboot fragment for RTT output.
Native-sim continues to disable MCUboot and program download.
