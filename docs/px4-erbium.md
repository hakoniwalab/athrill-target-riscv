# PX4 (AIFoundry Erbium) on Athrill

This guide builds the upstream PX4 `aifoundry_erbium_default` firmware and runs
it unmodified on Athrill's RISC-V target, which models the AIFoundry Erbium
board (RV64IMFC, 200 MHz, 2 MHz machine timer, Shakti UART, PLIC). PX4 starts
its SIH simulated quadcopter, EKF2 and the multicopter controllers on NuttX.
An optional second build sends MAVLink over the UART so that QGroundControl can
show the vehicle.

The commands below are meant to be typed in order. They were checked with the
versions listed at the end.

## Requirements

- macOS or Linux, on arm64 or x86_64
- Git, curl, tar, CMake 3.16 or later, a C/C++ compiler (Clang or GCC)
- Docker (PX4 is built in PX4's own build image)
- About 6 GB of free disk space (PX4 with its submodules and build)

## 1. Workspace and Athrill

Athrill is the core (`toppers/athrill`, branch `cpp`) next to the RISC-V
target (`hakoniwalab/athrill-target-riscv`).

```bash
mkdir -p "$HOME/athrill-px4"
cd "$HOME/athrill-px4"
git clone -b cpp https://github.com/toppers/athrill.git athrill
git clone https://github.com/hakoniwalab/athrill-target-riscv.git
cmake -S athrill-target-riscv -B athrill-build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build athrill-build --parallel
ls -l athrill-build/athrill3
```

## 2. RISC-V toolchain for PX4

PX4's build image `px4io/px4-dev:v1.17.0-beta1` does not contain the RISC-V
compiler that the Erbium board needs (`riscv-none-elf-gcc`). The xPack RISC-V
Embedded GCC 15.2.0-1 for Linux is used instead, matching the architecture of
the Docker engine (arm64 on Apple silicon, x64 on most PCs).

```bash
cd "$HOME/athrill-px4"
case "$(docker info --format '{{.Architecture}}')" in
  aarch64|arm64) XPACK_ARCH=arm64 ;;
  *) XPACK_ARCH=x64 ;;
esac
mkdir -p tools
curl -L --fail --retry 3 -o tools/xpack.tar.gz \
  "https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v15.2.0-1/xpack-riscv-none-elf-gcc-15.2.0-1-linux-${XPACK_ARCH}.tar.gz"
tar -xzf tools/xpack.tar.gz -C tools
rm tools/xpack.tar.gz
ls tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-gcc
```

The archive is the Linux toolchain for the build container; it is unpacked on
the host (unpacking it inside a container onto a host directory fails on
macOS).

## 3. Build PX4 for Erbium

The Erbium board is in PX4 `main`; this guide pins the commit it was checked
with.

```bash
cd "$HOME/athrill-px4"
git clone https://github.com/PX4/PX4-Autopilot.git PX4
git -C PX4 checkout 216039a12bab5a86769128f7be3e5f63dc7b4952
git -C PX4 submodule update --init --recursive
docker run --rm \
  -v "$PWD/PX4:/src" \
  -v "$PWD/tools/xpack-riscv-none-elf-gcc-15.2.0-1:/opt/riscv:ro" \
  -w /src px4io/px4-dev:v1.17.0-beta1 \
  bash -c 'export PATH=/opt/riscv/bin:$PATH && make aifoundry_erbium_default'
ls -l PX4/build/aifoundry_erbium_default/aifoundry_erbium_default.elf
```

## 4. Run PX4

`-c1` is one hart, `-m` the memory map (16 MiB at `0x40000000`), `-d` the
device configuration (200 MHz CPU, 2 MHz timer, clock skip while the CPU waits
in WFI) and `-t` the number of clocks to run: 2,000,000,000 clocks are 10 s of
simulated time. Without the debugger (no `-i`) Athrill runs as fast as it can.

```bash
cd "$HOME/athrill-px4"
athrill-build/athrill3 -c1 \
  -m athrill-target-riscv/test/erbium/memory.txt \
  -d athrill-target-riscv/test/erbium/device_config.txt \
  -t 2000000000 \
  PX4/build/aifoundry_erbium_default/aifoundry_erbium_default.elf
```

The console shows PX4 booting on NuttX and ends at the NSH prompt; Athrill
stops after the given clocks with `EXIT for timeout(2000000000).`:

```text
HW arch: AIFOUNDRY_ERBIUM
PX4 git-hash: 216039a12bab5a86769128f7be3e5f63dc7b4952
PX4 version: 1.18.0 80 (17956992)
OS: NuttX
...
Loading airframe: /etc/init.d/airframes/1100_rc_quad_x_sih.hil
...
ekf2 [172:237]
...
NuttShell (NSH)
nsh> INFO  [vehicle_imu] GYRO 0 (1310988) offset committed: ...
```

`... command not found` and `ERROR [param]` lines come from the generic start
scripts calling modules and parameters that the Erbium build does not contain.

### Typing commands without the debugger

`DEVICE_CONFIG_ERBIUM_UART_RX_FILE` feeds a file to the UART, one character
per 30 clocks of simulated time from the start of the run, so the same file
gives the same run every time. This waits 12 s of PX4 time and prints the task
list:

```bash
cd "$HOME/athrill-px4"
printf 'sleep 12\ntop once\n' > px4-commands.txt
cp athrill-target-riscv/test/erbium/device_config.txt px4-device-config.txt
echo "DEVICE_CONFIG_ERBIUM_UART_RX_FILE $PWD/px4-commands.txt" >> px4-device-config.txt
athrill-build/athrill3 -c1 \
  -m athrill-target-riscv/test/erbium/memory.txt \
  -d px4-device-config.txt \
  -t 3000000000 \
  PX4/build/aifoundry_erbium_default/aifoundry_erbium_default.elf
```

`top` lists `sih` (the simulated vehicle), `wq:rate_ctrl`, `wq:INS0` (EKF2),
`wq:nav_and_controllers`, `commander` and `navigator`, and an uptime of about
13.4 s.

## 5. QGroundControl (optional)

Erbium has one UART, used by the NSH console in the default build. The patch
`docs/px4/erbium-mavlink.patch` adds a board variant `aifoundry_erbium_mavlink`
that runs MAVLink on that UART instead (console and system log go to the RAM
log, readable with `dmesg` from the MAVLink shell). As the console then has
no input, the board initialisation gives NSH an empty pipe to read, so that
NSH keeps running after the start script. It also sets
`CONFIG_LINE_MAX=128`: with the default 80, the line in `rc.serial` that builds
the MAVLink arguments is cut off.

```bash
cd "$HOME/athrill-px4"
git -C PX4 apply "$PWD/athrill-target-riscv/docs/px4/erbium-mavlink.patch"
docker run --rm \
  -v "$PWD/PX4:/src" \
  -v "$PWD/tools/xpack-riscv-none-elf-gcc-15.2.0-1:/opt/riscv:ro" \
  -w /src px4io/px4-dev:v1.17.0-beta1 \
  bash -c 'export PATH=/opt/riscv/bin:$PATH && make aifoundry_erbium_mavlink'
ls -l PX4/build/aifoundry_erbium_mavlink/aifoundry_erbium_mavlink.elf
```

`DEVICE_CONFIG_ERBIUM_UART_TCP_PORT` makes Athrill connect the UART to a TCP
client on `127.0.0.1` (local host only). Athrill runs PX4 faster than real
time; `DEBUG_FUNC_REAL_TIME 1` keeps it at real time, so that the vehicle
in QGroundControl moves at its real speed. `-t -1` runs without a clock
limit; stop Athrill with Ctrl-C.

```bash
cd "$HOME/athrill-px4"
cp athrill-target-riscv/test/erbium/device_config.txt px4-qgc-device-config.txt
echo "DEVICE_CONFIG_ERBIUM_UART_TCP_PORT 5760" >> px4-qgc-device-config.txt
echo "DEBUG_FUNC_REAL_TIME 1" >> px4-qgc-device-config.txt
athrill-build/athrill3 -c1 \
  -m athrill-target-riscv/test/erbium/memory.txt \
  -d px4-qgc-device-config.txt \
  -t -1 \
  PX4/build/aifoundry_erbium_mavlink/aifoundry_erbium_mavlink.elf
```

In QGroundControl add a TCP link (Application Settings, Comm Links, Add: type
TCP, server `127.0.0.1`, port 5760) and connect. The vehicle appears in Hold
mode with the SIH GPS position. QGroundControl reports "not ready" because
there is no RC transmitter, and warns that HITL simulation is enabled (SIH
sets `SYS_HITL` 2); both are expected. Takeoff from QGroundControl arms the
vehicle and climbs to about 2.5 m, where it holds.

## Notes

- Simulated time: Athrill counts 200,000,000 clocks per simulated second
  (`DEVICE_CPU_FREQ 200`), and the machine timer runs at 2 MHz
  (`DEVICE_CONFIG_ERBIUM_TIMER_FREQ 2000000`), which must equal NuttX's
  `CONFIG_ERBIUM_MTIMER_FREQ`. PX4's uptime then equals Athrill's simulated
  time.
- Speed (Apple M-series, macOS): 15 s of PX4 time take about 7.3 s, i.e.
  about 2 times real time. Runs without the debugger are deterministic for the
  same ELF, configuration and input file.
- Erbium specifics that Athrill models: misaligned loads and stores are
  allowed; `fdiv.s`, `fsqrt.s` and the 64-bit float conversions raise
  exception 30 and NuttX emulates them; cycle and instret counters read 0.
- Found while running PX4 (default build): NuttX's `CONFIG_LINE_MAX` is 80 on
  Erbium (other PX4 boards use 128), which cuts long start-script lines; and
  quoted strings containing variables (`"... ${VAR} ..."`) in start scripts do
  not expand as on other boards.

## Checked versions

| Component | Version |
|---|---|
| Athrill core (`toppers/athrill`, branch `cpp`) | `e62695cc` |
| Athrill RISC-V target | see this repository's log |
| PX4 | `216039a12bab5a86769128f7be3e5f63dc7b4952` |
| PX4's NuttX | `172b8fd7dbcfa0ea29e3be5b09fe8a392ab843a6` |
| PX4 build image | `px4io/px4-dev:v1.17.0-beta1` |
| RISC-V toolchain | xPack `riscv-none-elf-gcc` 15.2.0-1 |
| QGroundControl | 5.0.8 |
