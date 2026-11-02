# Erbium device integration test

Build all test programs with `test/build-firmware.sh build/firmware` (see the
top-level README); the device test is `build/firmware/erbium-device-test.elf`
and runs as the `firmware-erbium-devices` CTest. It covers UART output, three
machine-timer interrupts, one received character through the PLIC, and the
TX-empty interrupt after the TX FIFO drains while the CPU waits in WFI with
clock skip enabled. The exact TX timing (16-character FIFO, one character per
30 clocks) is checked by the `athrill-uart-tx` unit test.

Run Athrill interactively:

```sh
build/athrill3 -i -c1 -m test/erbium/memory.txt \
  -d test/erbium/device_config.txt -t20000 build/firmware/erbium-device-test.elf
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
    -d test/erbium/device_config.txt -t20000 build/firmware/erbium-device-test.elf
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
