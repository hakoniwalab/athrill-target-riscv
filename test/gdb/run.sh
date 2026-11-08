#!/bin/sh
# GDB remote debugging with 64-bit addresses (toppers/athrill#86):
#   test/gdb/run.sh <athrill3> <gdb> <rv64i-selftest.elf> <port>
# memory.txt adds 4 KiB of RAM at 0x100000000, above 4 GiB. gdb writes a
# jump-to-self (0x0000006f) there, sets pc to it, breaks there, steps one
# instruction and continues: both stop at 0x100000000. Checks the target
# description (pc is 64 bits, ABI register names) and a register write.
# Address 0 is an ordinary breakpoint address (toppers/athrill#81): RAM at
# 0 holds "j +8" and at 8 "j 0"; with a breakpoint only at 8, running from
# 8 passes through 0 and stops at 8 again (the instruction a run resumes at
# is not checked, so the run must come back through 0); a breakpoint at 0 stops there, also after it
# was deleted and set again.
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
set *(unsigned int *)0x0 = 0x0080006f
set *(unsigned int *)0x8 = 0xff9ff06f
break *0x8
set var \$pc = 0x8
continue
printf "PASS0=%016lx\n", \$pc
delete
break *0x0
continue
printf "AT0=%016lx\n", \$pc
delete
break *0x0
continue
printf "AGAIN0=%016lx\n", \$pc
delete
detach
EOC
"$gdb" -batch -x "$work/commands" "$elf" > "$work/gdb.log" 2>&1 || true
cat "$work/gdb.log"
for expected in "WORD=0000006f" "PC=0000000100000000" "PC1=0000000100000000" "PC2=0000000100000000" \
	"A5=1122334455667788" "ZERO=0000000000000000" \
	"PASS0=0000000000000008" "AT0=0000000000000000" "AGAIN0=0000000000000000"; do
	if ! grep -q "$expected" "$work/gdb.log"; then
		echo "FAIL: $expected"
		exit 1
	fi
done
echo "PASS"
