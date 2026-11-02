#!/bin/sh
set -eu
out=${1:-.}
riscv64-unknown-elf-gcc -O2 -nostdlib -nostartfiles -ffreestanding \
  -march=rv64i_zicsr -mabi=lp64 -mcmodel=medany \
  -Wl,-T,link.ld -Wl,--build-id=none start.S selftest.c -o "$out/selftest.elf"
riscv64-unknown-elf-objdump -d "$out/selftest.elf" > "$out/selftest.dump"
riscv64-unknown-elf-gcc -O2 -nostdlib -nostartfiles -ffreestanding \
  -march=rv64i -mabi=lp64 -mcmodel=medany \
  -Wl,-T,link.ld -Wl,--build-id=none benchmark.S -o "$out/benchmark.elf"
