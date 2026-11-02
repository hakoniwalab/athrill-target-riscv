#!/bin/sh
# Needs the toolchain image: docker build -t athrill-riscv-toolchain:ubuntu24 tools/docker
set -eu
cd "$(dirname "$0")"
docker run --rm -v "$PWD:/work" -w /work athrill-riscv-toolchain:ubuntu24 \
  riscv64-unknown-elf-gcc -O2 -fno-builtin -nostdlib -nostartfiles -march=rv64im_zicsr \
  -mabi=lp64 -mcmodel=medany -T link.ld start.S device_test.c -o device_test.elf
