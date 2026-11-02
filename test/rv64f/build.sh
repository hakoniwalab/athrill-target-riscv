#!/bin/sh
set -eu
out=${1:-.}
riscv64-unknown-elf-gcc -O2 -nostdlib -nostartfiles -ffreestanding \
  -march=rv64if_zicsr -mabi=lp64 -mcmodel=medany \
  -Wl,-T,link.ld -Wl,--build-id=none start.S selftest.c -o "$out/fp-selftest.elf"
riscv64-unknown-elf-objdump -d "$out/fp-selftest.elf" > "$out/fp-selftest.dump"
