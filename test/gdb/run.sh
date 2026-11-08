#!/bin/sh
# GDB remote debugging with 64-bit addresses (toppers/athrill#86):
#   test/gdb/run.sh <athrill3> <gdb> <rv64i-selftest.elf> <port>
# memory.txt adds 4 KiB of RAM at 0x100000000, above 4 GiB. gdb writes a
# jump-to-self (0x0000006f) there, sets pc to it, breaks there, steps one
# instruction and continues: both stop at 0x100000000. Checks the target
# description (pc is 64 bits, ABI register names) and a register write.
set -eu
athrill=$1
gdb=$2
elf=$3
port=$4
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'kill "$pid" 2>/dev/null || true; rm -rf "$work"' EXIT INT TERM
"$athrill" -c1 -g"$port" -m "$here/memory.txt" -t 10000000 "$elf" > "$work/athrill.log" 2>&1 &
pid=$!
i=0
while ! grep -q "gdb server listening" "$work/athrill.log" 2>/dev/null && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
cat > "$work/commands" <<EOC
set pagination off
target remote 127.0.0.1:$port
set *(unsigned int *)0x100000000 = 0x0000006f
printf "WORD=%08x\n", *(unsigned int *)0x100000000
set var \$pc = 0x100000000
printf "PC=%016lx\n", \$pc
break *0x100000000
stepi
printf "PC1=%016lx\n", \$pc
continue
printf "PC2=%016lx\n", \$pc
set var \$a5 = 0x1122334455667788
printf "A5=%016lx\n", \$a5
printf "ZERO=%016lx\n", \$zero
delete
detach
EOC
"$gdb" -batch -x "$work/commands" "$elf" > "$work/gdb.log" 2>&1 || true
cat "$work/gdb.log"
for expected in "WORD=0000006f" "PC=0000000100000000" "PC1=0000000100000000" "PC2=0000000100000000" \
	"A5=1122334455667788" "ZERO=0000000000000000"; do
	if ! grep -q "$expected" "$work/gdb.log"; then
		echo "FAIL: $expected"
		exit 1
	fi
done
echo "PASS"
