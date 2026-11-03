# Athrill RISC-V target

This repository contains the RV64 target for Athrill. Its goal is to run PX4
on the AIFoundry Erbium board's NuttX environment, with Hakoniwa.

## Status

- **ISA:** RV64IMFC with Zicsr and Zifencei; Machine and User mode
  (`ecall`, `ebreak`, `mret`, `wfi`); 16 PMP entries (OFF, TOR, NA4, NAPOT).
  Supervisor CSRs are stored but Supervisor mode is not used yet.
- **Erbium behaviour** (default, `-DRISCV_ERBIUM_COMPAT=ON`): misaligned scalar
  loads/stores are allowed, cycle/retired-instruction CSRs read 0, and
  `fence.i`, `fdiv.s`, `fsqrt.s` and the 64-bit FP conversions raise the
  Erbium-specific cause 30; denormal inputs and results are flushed to zero,
  and a denormal input is reported in fcsr bit 31.
  `-DRISCV_ERBIUM_COMPAT=OFF` builds the standard RV64 behaviour used for
  comparisons with the Sail model.
- **Devices:** the Erbium UART (16-character TX FIFO sending one character
  every 30 clocks of the virtual clock, RX FIFO, TX-empty and RX interrupts),
  machine timer (`mtime`/`mtimecmp`, frequency from the device config), PLIC,
  system registers, and a bring-up byte output register at `0x10000000`.
  Device registers are 32-bit; every load or store touches each covered
  register once.
- **Software:** NuttX `minion:nsh` boots, NuttX `ostest` passes, and PX4
  `aifoundry_erbium_default` runs (see [PX4](#px4)).
- **Decoder:** mask/match tables generated from `riscv-opcodes` (see below),
  a two-level cold decoder, and a decode cache with one array of decoded
  instructions per 4 KiB code page (two-byte granularity), allocated on
  first execution. A cache hit is reused only at the same
  privilege and PMP setting it was fetched with, and only when the
  instruction bytes are unchanged. In batched execution, runs of cached
  instructions up to the next branch, jump or system instruction execute as
  one block without per-instruction checks; a store into a block's code
  drops it. Results and clocks are the same as one instruction at a time.

## PX4

[docs/px4-erbium.md](docs/px4-erbium.md) builds the upstream PX4
`aifoundry_erbium_default` firmware and runs it on Athrill (SIH quadcopter on
NuttX), with an optional MAVLink build for QGroundControl.

## Opcode generation

The generated decoder files are pinned to `riscv/riscv-opcodes` commit
`f24ac23ec9fa947011781a09cd6fd25a7c36dc45`, licensed BSD-3-Clause. Regenerate
the files from a checkout at that commit (Python 3.10 or later):

```sh
tools/generate_riscv_opcodes.py /path/to/riscv-opcodes
```

The selected sources are RV32/RV64 I, M, F, C, Zicsr, Zifencei, and system
instructions. Masks, matches and immediate layouts come from riscv-opcodes;
the selection of enabled instructions and the expansion of compressed
instructions into base instructions are the generator's own tables (MIT).
Each generated file states which parts come from where and carries the
riscv-opcodes license. Do not edit the generated files.

`test/crosscheck_decoder.py --dump build/athrill-decoder-dump` checks the
decoder against GNU objdump, whose RISC-V opcode tables are independent of
riscv-opcodes: sampled 32-bit words of every enabled instruction plus random
words, and all 16-bit compressed encodings with their expansion. It needs
the Docker toolchain image.

## Tests

`ctest` runs the host unit tests (usage, PMP, clock counters, decoder, device
register access, UART TX timing) and the core's malformed ELF test. The
bare-metal programs under `test/` (RV64I and RV64F self-tests, PMP and
decode-cache, CSR write, fetch at region ends, memory regions, Erbium devices,
benchmarks) are built with the RISC-V cross toolchain in Docker and then
registered as CTests automatically:

```sh
docker build -t athrill-riscv-toolchain:ubuntu24 tools/docker
test/build-firmware.sh build/firmware
cmake -S . -B build -DNUTTX_ELF=/path/to/nuttx   # NUTTX_ELF is optional
cmake --build build && (cd build && ctest)
```

For acceptance runs configure with `-DRISCV_FIRMWARE_TESTS=ON`: the firmware
is then built as part of the build and rebuilt when its sources change, the
objdump decoder cross-check becomes a CTest, and configuration fails if Docker
or Python is missing. Without it, a configure that finds no firmware warns
that the firmware tests are not registered.

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
region such as `ROM, 0x80000000, 64` (size is in KiB). `test/erbium/` has the
memory map and device config for Erbium programs such as NuttX.

## Licence and provenance

Target files are released under the MIT License in this repository. No
source file was copied from `athrill-target-v850e2m`, `athrill-target-rh850f1x`
or any other emulator (including AIFoundry sw-sysemu, which was used only as a
behaviour reference for Erbium devices and as a trace oracle); there are no
files carrying the TOPPERS License header. The generated decoder files contain
data from riscv-opcodes under its BSD-3-Clause license, as stated in each
file. The Athrill core remains under its own existing licence.
