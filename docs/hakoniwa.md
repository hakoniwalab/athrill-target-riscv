# PX4 (AIFoundry Erbium) with Hakoniwa Drone

PX4 on Athrill flies the Hakoniwa Drone x500 in MuJoCo (Processor-in-the-Loop):
- **Hakoniwa Drone** computes the vehicle and writes its sensor samples to Hakoniwa shared memory (`hil_sensor`,
  `hil_gps`).
- **The Hakoniwa sensor device** in Athrill gives them to PX4: Erbium has no sensor chips, so PX4's driver
  `hako_sensor_device` reads the device's registers.
- **PX4's motor outputs** go back to Hakoniwa Drone (`hil_actuator`).

Both sides move in lockstep on the data: Athrill runs the CPU up to the time of each sensor sample.

What you need, all public:
- [Hakoniwa Drone](https://github.com/toppers/hakoniwa-drone-core) v5.0.0 or later (free edition, or Hakoniwa Drone
  PRO): the aircraft service, the airframe `config/drone/x500-athrill-px4` and the PDU definition
  `config/pdudef/drone-pdudef-athrill-px4.json`;
- [athrill-device](https://github.com/hakoniwalab/athrill-device): the sensor device (`board/common`) and, for
  this board, the PX4 driver and board patch (`board/erbium`);
- this repository and PX4, set up as in [px4-erbium.md](px4-erbium.md) sections 1 to 3 and 5.

The commands below use the workspace of [px4-erbium.md](px4-erbium.md) (`$HOME/athrill-px4`).

## 1. Build the sensor device

Clone athrill-device next to `athrill`, then build the device for a 64-bit Athrill against the same Hakoniwa
Foundation as Hakoniwa Drone (with the Business Pack, `hakoniwa-business-pack/work/foundation/install`):

```bash
cd "$HOME/athrill-px4"
git clone https://github.com/hakoniwalab/athrill-device.git
git clone https://github.com/hakoniwalab/hakoniwa-pdu-registry.git
cmake -S athrill-device/board/common -B athrill-device/build-erbium -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HAKONIWA_FOUNDATION_PREFIX" -DATHRILL_ADDRESS_WIDTH=64 \
  -DATHRILL_SOURCE_DIR="$PWD/athrill" -DHAKO_PDU_REGISTRY_DIR="$PWD/hakoniwa-pdu-registry"
cmake --build athrill-device/build-erbium -j
```

## 2. Build PX4 for Hakoniwa

The board variant `aifoundry_erbium_hakoniwa` comes after the MAVLink patch of [px4-erbium.md](px4-erbium.md)
section 5. It has no SIH, and the x500 parameters of Hakoniwa Drone. The PX4 driver is an external module.

```bash
cd "$HOME/athrill-px4"
git -C PX4 apply "$PWD/athrill-device/board/erbium/px4/erbium-hakoniwa.patch"
git -C PX4/platforms/nuttx/NuttX/nuttx clean -fdXq
git -C PX4/platforms/nuttx/NuttX/apps clean -fdXq
docker run --rm \
  -v "$PWD/PX4:/src" \
  -v "$PWD/athrill-device/board/erbium/px4:/ext:ro" \
  -v "$PWD/athrill-device/board/common/include:/common/include:ro" \
  -v "$PWD/tools/xpack-riscv-none-elf-gcc-15.2.0-1:/opt/riscv:ro" \
  -w /src px4io/px4-dev:v1.17.0-beta1 \
  bash -c 'export PATH=/opt/riscv/bin:$PATH && make aifoundry_erbium_hakoniwa EXTERNAL_MODULES_LOCATION=/ext'
ls -l PX4/build/aifoundry_erbium_hakoniwa/aifoundry_erbium_hakoniwa.elf
```

## 3. Athrill settings

```bash
cd "$HOME/athrill-px4"
cp athrill-target-riscv/test/erbium/memory.txt hakoniwa-memory.txt
echo "DEV, 0x20000000, $PWD/athrill-device/build-erbium/libhako_sensor_device.dylib" >> hakoniwa-memory.txt
cp athrill-target-riscv/test/erbium/device_config.txt hakoniwa-device-config.txt
cat athrill-device/board/erbium/config/device_config-hakoniwa.txt >> hakoniwa-device-config.txt
echo "DEVICE_CONFIG_ERBIUM_UART_TCP_PORT 5760" >> hakoniwa-device-config.txt
```

On Linux the device is `libhako_sensor_device.so`.

## 4. Run

Start the three in this order. Hakoniwa Drone's aircraft service is the only conductor.

1. Hakoniwa Drone, from its directory (the free edition's `mac/` binaries shown):

   ```bash
   mac/mac-main_hako_aircraft_service_px4 127.0.0.1 4560 \
     config/drone/x500-athrill-px4 config/pdudef/drone-pdudef-athrill-px4.json
   ```

2. Athrill, with the Foundation's `lib` on `DYLD_LIBRARY_PATH` (`LD_LIBRARY_PATH` on Linux):

   ```bash
   cd "$HOME/athrill-px4"
   athrill-build/athrill3 -c1 -m hakoniwa-memory.txt -d hakoniwa-device-config.txt -t -1 \
     PX4/build/aifoundry_erbium_hakoniwa/aifoundry_erbium_hakoniwa.elf
   ```

3. `hako-cmd start`.

QGroundControl or pymavlink on `tcp:127.0.0.1:5760`.

**Take-off over MAVLink:** the altitude of `MAV_CMD_NAV_TAKEOFF` is above mean sea level. Give the home altitude
plus the height to climb. QGroundControl does this for you.

## Checked

- 2026-11-03, Hakoniwa Drone PRO, macOS: the x500 arms, takes off to 2.5 m and holds; RTF 2.14 on the ground,
  2.18 hovering (no viewer).
- 2026-11-05, Hakoniwa Drone v5.0.0 (sensor model v2), macOS: the data lockstep runs (each `hil_sensor`
  answered by one `hil_actuator`). A flight on this board with v5.0.0 is not checked yet.
