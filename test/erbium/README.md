# Erbium device integration test

Build with the Docker cross-toolchain image:

```sh
./test/erbium/build.sh
```

Run Athrill interactively:

```sh
build/athrill3 -i -c1 -m test/erbium/memory.txt \
  -d test/erbium/device_config.txt -t20000 test/erbium/device_test.elf
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
    -d test/erbium/device_config.txt -t20000 test/erbium/device_test.elf
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
