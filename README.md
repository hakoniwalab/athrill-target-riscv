# Athrill RISC-V target

This repository contains the RV64 target for Athrill. Its intended architecture
is RV64IMFC. The longer-term goal is to run PX4 on the AIFoundry Erbium board's
NuttX environment; Erbium peripherals and instruction semantics are deliberately
outside this initial skeleton.

## Status

The target implements RV64I, Zicsr, Zifencei, and the M-mode `mret` and `wfi`
instructions. The decoder uses mask/match constants generated from
`riscv-opcodes` and a two-level opcode/funct3 dispatch table. M, F, and C
encodings are included in the generated header for the next implementation
stages, but are not advertised in `misa` or executable yet.

The model includes one RV64 hart, integer and floating-point register storage,
machine/supervisor CSR storage, and 16 PMP entries supporting OFF, TOR, NA4, and
NAPOT. Devices are intentionally limited to a clock/interrupt-controller stub
and a temporary byte-oriented serial output register at `0x10000000`. Writing
the low byte there prints it to standard output. This register will be replaced
by the Erbium UART model.

Loads, stores, and taken control transfers use strict natural alignment. A
misaligned access traps with instruction/load/store address-misaligned cause
rather than being emulated as multiple accesses. `fence` and `fence.i` are
no-ops for the current single-hart model; there is no decoded-instruction cache
to invalidate.

## Opcode generation

The generated header is pinned to `riscv/riscv-opcodes` commit
`f24ac23ec9fa947011781a09cd6fd25a7c36dc45`, licensed BSD-3-Clause. Regenerate
it from a checkout at that commit with:

```sh
tools/generate_riscv_opcodes.py /path/to/riscv-opcodes
```

The selected sources are RV32/RV64 I, M, F, C, Zicsr, Zifencei, and system
instructions. The generated header carries the upstream license text and exact
provenance.

## RV64I bare-metal test

`test/rv64i` contains a freestanding startup, linker script, memory map,
self-test, and tight-loop benchmark. Build them in an Ubuntu container with
`gcc-riscv64-unknown-elf`, then run the self-test with:

```sh
docker run --rm -v "$PWD/test/rv64i:/work" -w /work ubuntu:24.04 sh -c \
  'apt-get update -qq && apt-get install -y -qq gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf && ./build.sh /work'
build/riscv/athrill3 -c1 -m test/rv64i/memory.txt -t10000 test/rv64i/selftest.elf
```

## Checkout and build

Place this repository next to the Athrill core:

```text
workspace/
  athrill/
  athrill-target-riscv/
```

Then build and test with:

```sh
cmake -S athrill-target-riscv -B build/riscv \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/riscv --parallel
ctest --test-dir build/riscv --output-on-failure
```

Use `-DATHRILL_SOURCE_DIR=/path/to/athrill` for a non-sibling core checkout.
The common `ATHRILL_ENABLE_EXDEV`, `ATHRILL_ENABLE_HAKONIWA`, and
`ATHRILL_ENABLE_LTO` options are passed through to the core build.

A minimal run looks like:

```sh
build/riscv/athrill3 -c1 -m memory.txt -t 1000 firmware.elf
```

For example, firmware linked at `0x80000000` needs a map containing a covering
region such as `ROM, 0x80000000, 64` (size is in KiB).

## Licence and provenance

New target files are released under the MIT License in this repository. No
source file in this skeleton was copied from `athrill-target-v850e2m` or
`athrill-target-rh850f1x`; those projects were used only as interface and design
references. Consequently there are currently no copied files carrying the
TOPPERS License header. The Athrill core remains under its own existing licence.
