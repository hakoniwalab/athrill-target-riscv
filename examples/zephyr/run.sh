#!/bin/sh
# Runs a Zephyr application built for Zephyr's erbium board on Athrill, on
# athrill-device's boards/erbium/erbium.dtb:
#   examples/zephyr/run.sh <zephyr.elf> [virtual seconds] [athrill3]
# The console (UART0) is Athrill's console. Without virtual seconds it runs
# until Ctrl+C.
set -eu
if [ $# -lt 1 ]; then
	echo "usage: $0 <zephyr.elf> [virtual seconds] [athrill3]" >&2
	exit 1
fi
HERE=$(cd "$(dirname "$0")" && pwd)
ELF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SECONDS_LIMIT=${2:-}
ATHRILL=${3:-$HERE/../../build/athrill3}
ATHRILL=$(cd "$(dirname "$ATHRILL")" && pwd)/$(basename "$ATHRILL")
DEVICE=${ATHRILL_DEVICE_DIR:-$(cd "$HERE/../../.." && pwd)/athrill-device}
DTB=$DEVICE/boards/erbium/erbium.dtb
if [ ! -f "$DTB" ]; then
	echo "no $DTB: clone hakoniwalab/athrill-device next to this repository or set ATHRILL_DEVICE_DIR" >&2
	exit 1
fi
MHZ=200
RUN=$HERE/work/run
mkdir -p "$RUN"
cd "$RUN"
echo "RAM, 0x40000000, 16384" > memory.txt
# Zephyr's idle thread on this board polls mip & mie in arch_cpu_idle()
# (no wfi): with it as the idle function, the CPU halts there while nothing
# is pending and the clocks to the next interrupt are skipped. The symbol
# needs DISABLE_SYMBOL_UNDERSCORE 1: by default Athrill drops a leading
# character from every symbol name (the V850 underscore).
cat > device_config.txt <<EOF
DEVICE_CPU_FREQ $MHZ
DEBUG_FUNC_ENABLE_SKIP_CLOCK 1
DEVICE_CONFIG_DEVICE_TREE $DTB
DISABLE_SYMBOL_UNDERSCORE 1
DEVICE_CONFIG_IDLE_FUNCTION arch_cpu_idle
EOF
if [ -n "$SECONDS_LIMIT" ]; then
	exec "$ATHRILL" -c1 -m memory.txt -d device_config.txt -t $((MHZ * 1000000 * SECONDS_LIMIT)) "$ELF"
fi
exec "$ATHRILL" -c1 -m memory.txt -d device_config.txt -t -1 "$ELF"
