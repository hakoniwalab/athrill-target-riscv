# Athrill RISC-V target

This repository contains the RV64 target for Athrill. Its intended architecture
is RV64IMFC. The longer-term goal is to run PX4 on the AIFoundry Erbium board's
NuttX environment; Erbium peripherals and instruction semantics are deliberately
outside this initial skeleton.

## Status

The target implements RV64I, Zicsr, Zifencei, and the M-mode `mret` and `wfi`
instructions. The decoder uses mask/match constants generated from
`riscv-opcodes`, a two-level cold decoder, and a direct-mapped predecode cache.
M, F, and C
encodings are included in the generated header for the next implementation
stages, but are not advertised in `misa` or executable yet.

The model includes one RV64 hart, integer and floating-point register storage,
machine/supervisor CSR storage, and 16 PMP entries supporting OFF, TOR, NA4, and
NAPOT. Devices are intentionally limited to a clock/interrupt-controller stub
and a temporary byte-oriented serial output register at `0x10000000`. Writing
the low byte there prints it to standard output. This register will be replaced
by the Erbium UART model.

The default `CPU_CONFIG_ERBIUM_COMPAT=1` personality accepts byte-split
misaligned scalar loads/stores, returns zero from cycle/retired-instruction
CSRs, and raises cause 30 for `fence.i`; `fence` remains a no-op. Set that
single compile-time switch to zero for standard RV64 behavior used by Sail
comparisons. Athrill's internal clock and retirement counters remain live.
`wfi` uses the existing halt path and is released by an enabled pending
interrupt.

The 2,048-entry cache is indexed at two-byte PC granularity for future C
support and stores the executor plus extracted operands and immediate. CPU
stores invalidate overlapping slots. Hits compare live instruction bytes,
reset clears all entries, and PMP CSR changes advance a permission epoch, so
loader/Hakoniwa reloads and protection changes need no core-side hook.

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

## Tests

`ctest` runs the host unit tests (usage, PMP, clock counters, decoder). The
bare-metal programs under `test/` (RV64I and RV64F self-tests, benchmarks,
Erbium device test) are built with the RISC-V cross toolchain in Docker and
then registered as CTests automatically:

```sh
docker build -t athrill-riscv-toolchain:ubuntu24 tools/docker
test/build-firmware.sh build/firmware
cmake -S . -B build -DNUTTX_ELF=/path/to/nuttx   # NUTTX_ELF is optional
cmake --build build && (cd build && ctest)
```

Each self-test prints `PASS <check>` / `FAIL <check>` lines and ends with
`SELFTEST PASS` or `SELFTEST FAIL <count>`; `test/run_firmware_test.py`
fails the CTest unless `SELFTEST PASS` appears with no `FAIL` line. With
`NUTTX_ELF` set, the NuttX test boots `minion:nsh`, types `help` and
expects the builtin-app list. `-DRISCV_ERBIUM_COMPAT=OFF` builds the
standard RV64 behaviour (only the standard FP self-test applies).

riscv-tests can be run with `test/run-riscv-tests.sh <athrill3> <riscv-tests/isa>`.

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
