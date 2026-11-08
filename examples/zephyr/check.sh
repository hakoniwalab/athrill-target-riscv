#!/bin/sh
# Runs Zephyr's samples on the Erbium board and checks their output:
#   examples/zephyr/check.sh [elf directory] [athrill3]
# The ELF directory holds <sample>-erbium.elf (build.sh writes work/elf; the
# release has the same files). Checks:
#   hello_world      prints "Hello World! erbium/erbium"
#   synchronization  thread_a and thread_b take turns, a greeting every
#                    0.5 s: 9 greetings in 5 virtual seconds, alternating
#   philosophers     all six philosophers eat at least 10 times in 30
#                    virtual seconds
# Exit status 0 when all pass. A run's output is capped at 4 MB.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ELFS=${1:-$HERE/work/elf}
ATHRILL=${2:-$HERE/../../build/athrill3}
OUT=$HERE/work/check
mkdir -p "$OUT"
FAILED=0
result() {	# PASS/FAIL, text
	echo "  [$1] $2"
	[ "$1" = PASS ] || FAILED=1
}
run() {	# sample, virtual seconds -> output file
	"$HERE/run.sh" "$ELFS/$1-erbium.elf" "$2" "$ATHRILL" 2>&1 | head -c 4000000 > "$OUT/$1-erbium.txt"
	echo "$OUT/$1-erbium.txt"
}
echo "erbium"
f=$(run hello_world 1)
if grep -aq "Hello World! erbium/erbium" "$f"; then
	result PASS "hello_world: Hello World! erbium/erbium"
else
	result FAIL "hello_world: no greeting (see $f)"
fi
f=$(run synchronization 5)
lines=$(grep -a -o 'thread_[ab]: Hello World from cpu 0' "$f" | cut -c8)
n=$(echo "$lines" | grep -c .)
turns=$(echo "$lines" | tr -d '\n')
if [ "$n" = 9 ] && [ "$turns" = ababababa ]; then
	result PASS "synchronization: 9 greetings in 5 s, alternating"
else
	result FAIL "synchronization: $n greetings ($turns), want 9 alternating (see $f)"
fi
f=$(run philosophers 30)
ate=$(grep -a -o 'Philosopher [0-9] \[[^]]*\] *EATING' "$f" | awk '{ n[$2]++ } END { for (i = 0; i < 6; i++) if (n[i] >= 10) printf "%d", i }')
if [ "$ate" = 012345 ]; then
	result PASS "philosophers: all six ate at least 10 times in 30 s"
else
	result FAIL "philosophers: only $ate ate 10 times in 30 s (see $f)"
fi
[ $FAILED = 0 ] && echo "ALL PASS" || echo "SOME CHECKS FAILED"
exit $FAILED
