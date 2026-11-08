# PX4 (AIFoundry Erbium) with Hakoniwa Drone

PX4 on Athrill flies the Hakoniwa Drone x500 in MuJoCo (Processor-in-the-Loop):
- **Hakoniwa Drone** computes the vehicle and writes its sensor samples to Hakoniwa shared memory (`hil_sensor`,
  `hil_gps`).
- **The Hakoniwa sensor device** in Athrill gives them to PX4: Erbium has no sensor chips, so PX4's driver
  `hako_sensor_device` reads the device's registers.
- **PX4's motor outputs** go back to Hakoniwa Drone (`hil_actuator`).

Both sides move in lockstep on the data: Athrill runs the CPU up to the time of each sensor sample.

What you need, all public:
- [Hakoniwa Drone](https://github.com/toppers/hakoniwa-drone-core) v5.0.0 or later, the free edition (Hakoniwa
  Drone PRO works the same way): the aircraft service, the airframe `config/drone/x500-athrill-px4` and the PDU
  definition `config/pdudef/drone-pdudef-athrill-px4.json`;
- [hakoniwa-core-pro](https://github.com/hakoniwalab/hakoniwa-core-pro), the Hakoniwa core, at the revision
  Hakoniwa Drone was built with;
- [athrill-device](https://github.com/hakoniwalab/athrill-device): the board's Devicetree
  (`boards/erbium/erbium-hakoniwa.dtb`), the sensor device (`board/common`), which Athrill loads as the board's
  plugin, and, for this board, the PX4 driver and board patch (`board/erbium`);
- this repository's `athrill3` and the PX4 ELF `aifoundry_erbium_hakoniwa.elf`, from the release (or built, see
  [Build PX4 yourself](#build-px4-yourself)).

The commands are for macOS on Apple silicon. On Linux x86_64, see [On Linux](#on-linux) for the few differences.
You need git, CMake, a C/C++ compiler, python3 with `pymavlink` (for the take-off check), and GLFW for Hakoniwa
Drone (`brew install glfw`). Everything goes into one workspace:

```bash
mkdir -p "$HOME/athrill-hakoniwa"
cd "$HOME/athrill-hakoniwa"
git clone --branch v0.2.0 https://github.com/hakoniwalab/athrill-target-riscv.git
```

## 1. Hakoniwa core

Hakoniwa Drone's binaries carry the core's libraries, but the sensor device is built against the core, and
`hako-cmd` starts the simulation. Install the same revision as Hakoniwa Drone v5.0.0 was built with:

```bash
cd "$HOME/athrill-hakoniwa"
git clone https://github.com/hakoniwalab/hakoniwa-core-pro.git
git -C hakoniwa-core-pro checkout "$(cat athrill-target-riscv/examples/px4-hakoniwa/hakoniwa-core-pro-revision.txt)"
git -C hakoniwa-core-pro submodule update --init --recursive
(cd hakoniwa-core-pro && ./install.bash)
```

`install.bash` builds the core and installs it in `/usr/local/hakoniwa`, with its configuration in `/etc/hakoniwa`
(it asks for your password for `sudo`).

## 2. Hakoniwa Drone

The repository of the free edition for the airframe and the PDU definition, and the binaries of its release:

```bash
cd "$HOME/athrill-hakoniwa"
git clone --branch v5.0.0 https://github.com/toppers/hakoniwa-drone-core.git
cd hakoniwa-drone-core
curl -LO https://github.com/toppers/hakoniwa-drone-core/releases/download/v5.0.0/mac.zip
unzip -q mac.zip
```

## 3. The sensor device

Built for this board (64-bit Athrill), against the core of step 1, with the PDU types of the same core:

```bash
cd "$HOME/athrill-hakoniwa"
git clone https://github.com/toppers/athrill.git
git -C athrill checkout bc8d76a
git clone https://github.com/hakoniwalab/athrill-device.git
git -C athrill-device checkout ff23daf
cmake -S athrill-device/board/common -B athrill-device/build-erbium -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/usr/local/hakoniwa -DATHRILL_ADDRESS_WIDTH=64 \
  -DHAKO_PDU_REGISTRY_DIR="$PWD/hakoniwa-core-pro/hakoniwa-pdu-registry"
cmake --build athrill-device/build-erbium -j
```

## 4. athrill3 and PX4

From this repository's release v0.2.0:

```bash
cd "$HOME/athrill-hakoniwa"
curl -LO https://github.com/hakoniwalab/athrill-target-riscv/releases/download/v0.2.0/athrill3-v0.2.0-macos-arm64.tar.gz
curl -LO https://github.com/hakoniwalab/athrill-target-riscv/releases/download/v0.2.0/aifoundry_erbium_hakoniwa.elf
curl -LO https://github.com/hakoniwalab/athrill-target-riscv/releases/download/v0.2.0/SHA256SUMS
shasum -a 256 -c --ignore-missing SHA256SUMS
tar xzf athrill3-v0.2.0-macos-arm64.tar.gz
```

`aifoundry_erbium_hakoniwa` is PX4 for Erbium without SIH, with the airframe x500 v2 and the parameters of the
Hakoniwa Drone x500 built in, and MAVLink on the UART.

## 5. Athrill settings

The board is athrill-device's `erbium-hakoniwa.dtb`: the Erbium with the sensor device's registers at
`0x20000000`, raising PLIC source 5. The sensor device you built is its plugin:

```bash
cd "$HOME/athrill-hakoniwa"
cp athrill-target-riscv/test/erbium/memory.txt hakoniwa-memory.txt
cp athrill-target-riscv/test/erbium/device_config.txt hakoniwa-device-config.txt
cat athrill-device/board/erbium/config/device_config-hakoniwa.txt >> hakoniwa-device-config.txt
echo "DEVICE_CONFIG_ERBIUM_UART_TCP_PORT 5760" >> hakoniwa-device-config.txt
echo "DEVICE_CONFIG_DEVICE_TREE $PWD/athrill-device/boards/erbium/erbium-hakoniwa.dtb" >> hakoniwa-device-config.txt
echo "DEVICE_CONFIG_DEVICE_PLUGIN_0 hakoniwa%2Csensor-registers=$PWD/athrill-device/build-erbium/libhako_sensor_device.dylib" >> hakoniwa-device-config.txt
```

## 6. Fly

If a Hakoniwa core of another version ran on this machine before, its runtime files are still in
`/var/lib/hakoniwa/mmap`. Clear them once, with no Hakoniwa process running:

```bash
/usr/local/hakoniwa/bin/hako-cmd recover
```

Otherwise the aircraft service stops at start with `HAKONIWA_FAIL_STOP ... (legacy flock.bin)`.


Three terminals. In the first, Hakoniwa Drone's aircraft service: the only conductor, with the MuJoCo viewer
at real time.

```bash
cd "$HOME/athrill-hakoniwa/hakoniwa-drone-core"
mac/mac-main_hako_aircraft_service_px4 127.0.0.1 4560 \
  config/drone/x500-athrill-px4 config/pdudef/drone-pdudef-athrill-px4.json --mujoco-viewer --real-sleep-msec 1
```

In the second, PX4 on Athrill:

```bash
cd "$HOME/athrill-hakoniwa"
DYLD_LIBRARY_PATH=/usr/local/hakoniwa/lib athrill3-v0.2.0-macos-arm64/athrill3 -c1 \
  -m hakoniwa-memory.txt -d hakoniwa-device-config.txt -t -1 aifoundry_erbium_hakoniwa.elf
```

In the third, start the simulation, then take off to 2.5 m and check the hover:

```bash
cd "$HOME/athrill-hakoniwa"
/usr/local/hakoniwa/bin/hako-cmd start
python3 athrill-target-riscv/examples/px4-hakoniwa/fly.py
```

`fly.py` prints `PASS` when the x500 holds 2.5 m (within 0.3 m) for the last 10 s of a 40 s hover. You can also
connect QGroundControl to TCP `127.0.0.1:5760` instead, and arm and take off from there.

**Take-off over MAVLink:** the altitude of `MAV_CMD_NAV_TAKEOFF` is above mean sea level. Give the home altitude
plus the height to climb (`fly.py` does). QGroundControl does this for you.

To stop, press Ctrl+C in the first two terminals, then run `/usr/local/hakoniwa/bin/hako-cmd recover`.

## On Linux

- Hakoniwa Drone: `lnx.zip` in place of `mac.zip`, and `lnx/linux-main_hako_aircraft_service_px4`. `lnx.zip`
  does not carry the core's libraries: put `/usr/local/hakoniwa/lib` on `LD_LIBRARY_PATH` for it too. It needs
  `libglfw3` and `libopengl0`.
- The sensor device is `libhako_sensor_device.so`.
- `athrill3-v0.2.0-linux-x86_64.tar.gz`, checked with `sha256sum -c --ignore-missing SHA256SUMS`.
- `LD_LIBRARY_PATH` in place of `DYLD_LIBRARY_PATH` for Athrill.

## Build PX4 yourself

Set up PX4 and the toolchain as in [px4-erbium.md](px4-erbium.md) sections 2, 3 and 5 (in the workspace
`$HOME/athrill-px4` used there, with athrill-device cloned next to it). The board variant `aifoundry_erbium_hakoniwa`
comes after the MAVLink patch of section 5. The PX4 driver is an external module.

```bash
cd "$HOME/athrill-px4"
git clone https://github.com/hakoniwalab/athrill-device.git
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

## Checked

- 2026-11-03, Hakoniwa Drone PRO, macOS: the x500 arms, takes off to 2.5 m and holds; RTF 2.14 on the ground,
  2.18 hovering (no viewer).
- 2026-11-05, macOS (Apple M2 Pro), the steps above with:
  - athrill3 and `aifoundry_erbium_hakoniwa.elf` built as in [px4-erbium.md](px4-erbium.md) and
    [Build PX4 yourself](#build-px4-yourself), from a fresh clone of PX4;
  - the sensor device of step 3, against hakoniwa-core-pro `ed26571`;
  - in place of `mac.zip` (Hakoniwa Drone v5.0.0 was not released yet), Hakoniwa Drone PRO built from the same
    v5.0.0 code, run with the libraries of that core.

  `fly.py` passed three times in a row, with the MuJoCo viewer at real time: 2.50 to 2.51 m in the last 10 s.
- 2026-11-05, after the sensor device reports Athrill's own time to Hakoniwa (athrill-device #3), macOS, released
  Hakoniwa Drone v5.0.0 `mac.zip` and this release's `athrill3` and `aifoundry_erbium_hakoniwa.elf`: `fly.py` 3/3
  PASS, 2.50 m, viewer on. Without the viewer the whole system runs at **RTF 2.07 to 2.11** while hovering.
