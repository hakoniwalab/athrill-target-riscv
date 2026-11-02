#!/bin/sh
#
# Build every bare-metal test program into OUTPUT_DIR with the RISC-V
# cross toolchain image (docker build -t athrill-riscv-toolchain:ubuntu24
# tools/docker). CMake picks the programs up from that directory
# (RISCV_FIRMWARE_DIR, default <build>/firmware) and registers CTests.
#
# usage: test/build-firmware.sh OUTPUT_DIR
set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: $0 OUTPUT_DIR" >&2
	exit 2
fi
mkdir -p "$1"
output=$(cd "$1" && pwd)
root=$(cd "$(dirname "$0")/.." && pwd)
image=athrill-riscv-toolchain:ubuntu24

docker run --rm -v "$root/test:/test:ro" -v "$output:/out" -w /test "$image" sh -eu -c '
gcc=riscv64-unknown-elf-gcc
common="-O2 -nostdlib -nostartfiles -ffreestanding -mabi=lp64 -mcmodel=medany -Wl,--build-id=none"

$gcc $common -march=rv64i_zicsr_zifencei -Wl,-T,rv64i/link.ld \
	rv64i/start.S rv64i/selftest.c -lgcc -o /out/rv64i-selftest.elf
$gcc $common -march=rv64i -Wl,-T,rv64i/link.ld \
	rv64i/benchmark.S -lgcc -o /out/rv64i-benchmark.elf
$gcc $common -march=rv64imc -Wl,-T,rv64i/link.ld \
	rv64i/benchmark_compressed.S -lgcc -o /out/rv64i-benchmark-compressed.elf
$gcc $common -march=rv64if_zicsr -DERBIUM_MODE=1 -Wl,-T,rv64f/link.ld \
	rv64f/start.S rv64f/selftest.c -lgcc -o /out/rv64f-selftest-erbium.elf
$gcc $common -march=rv64if_zicsr -DERBIUM_MODE=0 -Wl,-T,rv64f/link.ld \
	rv64f/start.S rv64f/selftest.c -lgcc -o /out/rv64f-selftest-standard.elf
$gcc -O2 -fno-builtin -nostdlib -nostartfiles -march=rv64im_zicsr -mabi=lp64 \
	-mcmodel=medany -T erbium/link.ld erbium/start.S erbium/device_test.c \
	-o /out/erbium-device-test.elf
'
echo "firmware built in $output"
