# Zephyr samples on Athrill RISC-V

Zephyr applications built for Zephyr's own `erbium` board (OpenHW Foundation
/ AIFoundry Erbium, RV64) run on Athrill unchanged, on athrill-device's
`boards/erbium/erbium.dtb`, the board PX4 runs on. Zephyr's Erbium support
(board, SoC, the Erbium UART driver) is upstream at the pinned revision.
The Athrill board includes the same Zephyr `erbium.dtsi`. Nothing is added
for Zephyr: no new board files, models or Devicetree changes.

Checked with three of Zephyr's samples:

| Sample | What `check.sh` requires |
| --- | --- |
| `hello_world` | `Hello World! erbium/erbium` |
| `synchronization` | two threads take turns through semaphores, one greeting every 0.5 s: 9 alternating greetings in 5 virtual seconds |
| `philosophers` | six threads of different priorities share forks through mutexes: every philosopher eats at least 10 times in 30 virtual seconds |

## Run

The release has the three ELFs (`<sample>-erbium.elf`). To run one:

```sh
examples/zephyr/run.sh hello_world-erbium.elf 1
```

The last argument is a limit in virtual seconds; without it the
application runs until Ctrl+C. The console is UART0, Athrill's console.
To check all three:

```sh
examples/zephyr/check.sh <directory with the ELFs>
```

Both scripts take `boards/erbium/erbium.dtb` from athrill-device next to
this repository (or `ATHRILL_DEVICE_DIR`) and `build/athrill3` (or the last
argument).

## Build the samples

```sh
examples/zephyr/build.sh
```

The script needs Docker, git and curl. It fetches Zephyr at the revision of
`revisions.txt`, the revision the athrill-device Erbium board includes. It
downloads the Zephyr SDK's `riscv64-zephyr-elf` compiler and checks it
against the SDK's `sha256.sum`, then builds the samples unchanged in a
small Ubuntu image (`Dockerfile`). The ELFs go to `work/elf/`.

Zephyr's `erbium` board describes the CPU as RV64IMC with Zicsr and
Zifencei. Like Athrill RISC-V, it has no atomic (A) extension.
Zephyr then uses its C atomics, and the three ELFs contain no atomic
instruction.

## Licenses of the ELFs

The ELFs contain Zephyr (Apache-2.0), and picolibc and libgcc from the
Zephyr SDK (BSD-style licenses, and the GCC Runtime Library Exception).
`revisions.txt` and `build.sh` identify the sources.
