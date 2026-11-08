# Erbium device integration test

Build all test programs with `test/build-firmware.sh build/firmware` (see the
top-level README); the device test is `build/firmware/erbium-device-test.elf`
and runs as the `firmware-erbium-devices` CTest. It covers UART output, three
machine-timer interrupts, one received character through the PLIC, and the
TX-empty interrupt after the TX FIFO drains while the CPU waits in WFI with
clock skip enabled. The devices are athrill-device models built from its Erbium
Devicetree; the CTest adds `DEVICE_CONFIG_DEVICE_TREE` to `device_config.txt`.
The exact TX timing (16-character FIFO, one character per 30 clocks) is checked
by athrill-device's `shakti-uart` unit test.

Run Athrill interactively:

```sh
cp test/erbium/device_config.txt build/erbium-dt.txt
echo "DEVICE_CONFIG_DEVICE_TREE ../athrill-device/boards/erbium/erbium.dtb" >> build/erbium-dt.txt
build/athrill3 -i -c1 -m test/erbium/memory.txt \
  -d build/erbium-dt.txt -t20000 build/firmware/erbium-device-test.elf
```

At the initial `[DBG>` prompt, inject a character through Athrill's serial-input
path and start the CPU:

```text
serial 0 Z
cont
```

To feed a file, turn its contents into the same debugger command (the UART FIFO
holds 16 bytes at a time):

```sh
printf 'serial 0 %s\ncont\n' "$(cat test/erbium/input.txt)" |
  build/athrill3 -i -c1 -m test/erbium/memory.txt \
    -d build/erbium-dt.txt -t20000 build/firmware/erbium-device-test.elf
```

Expected device output begins:

```text
erbium device test
plic armed
timer armed
echo Z
interrupts on
timer 1
timer 2
timer 3
PASS
```

## Input without the debugger

For runs with `-t` (no debugger), add to the device config

```text
DEVICE_CONFIG_ERBIUM_UART_RX_FILE /path/to/input.txt
```

The UART receives the file from its start, one character per 30 clocks of the
virtual clock while the RX FIFO has room, so the same file arrives at the same
virtual times on every run. Data appended later is picked up within about a
million clocks. The `firmware-nuttx-nsh-file-input` CTest runs NuttX this way
with `nsh_help_input.txt`.

## UART over TCP

With

```text
DEVICE_CONFIG_ERBIUM_UART_TCP_PORT 5760
```

Athrill listens on 127.0.0.1:5760 (local host only) for one client and connects
it to the UART: received bytes enter the RX FIFO one per character period,
transmitted bytes are sent about every 10 us of virtual time. With the PX4
Erbium build that runs MAVLink on UART0, QGroundControl connects through a TCP
link to 127.0.0.1:5760. Use `-t -1` to run without a clock limit.
