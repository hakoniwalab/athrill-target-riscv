# Athrill RISC-V target

This repository contains the RV64 target for Athrill. Its intended architecture
is RV64IMFC. The longer-term goal is to run PX4 on the AIFoundry Erbium board's
NuttX environment; Erbium peripherals and instruction semantics are deliberately
outside this initial skeleton.

## Status

The target is currently a buildable bring-up skeleton. It loads a little-endian
RV64 ELF executable whose physical addresses fit in Athrill's current 32-bit bus,
sets the reset PC from the ELF entry point, fetches 16-bit compressed or 32-bit
instructions, and raises the RISC-V illegal-instruction exception for every
instruction. The empty table-driven decode layer is reserved for generated Sail
RISC-V decoder/executor code.

The model includes one RV64 hart, integer and floating-point register storage,
machine/supervisor CSR storage, and 16 PMP entries supporting OFF, TOR, NA4, and
NAPOT. Devices are intentionally limited to a clock/interrupt-controller stub
and a temporary byte-oriented serial output register at `0x10000000`. Writing
the low byte there prints it to standard output. This register will be replaced
by the Erbium UART model.

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
