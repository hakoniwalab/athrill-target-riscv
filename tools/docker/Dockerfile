# RISC-V cross toolchain used by the bare-metal tests under test/.
# Build once:
#   docker build -t athrill-riscv-toolchain:ubuntu24 tools/docker
FROM ubuntu:24.04
RUN apt-get update -qq \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
      gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf \
 && rm -rf /var/lib/apt/lists/*
