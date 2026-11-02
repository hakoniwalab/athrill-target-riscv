#!/bin/sh
set -eu
if [ "$#" -lt 2 ]; then
  echo "usage: $0 ATHRILL3 RISCV_TESTS_ISA [test-glob ...]" >&2
  exit 2
fi
athrill=$1
isa=$2
shift 2
[ "$#" -gt 0 ] || set -- 'rv64ui-p-*' 'rv64um-p-*' 'rv64uc-p-*'
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
status=0
for pattern do
  for elf in "$isa"/$pattern; do
    [ -f "$elf" ] || continue
    case "$elf" in *.dump) continue;; esac
    out=$(mktemp "${TMPDIR:-/tmp}/athrill-riscv.XXXXXX")
    "$athrill" -c1 -m "$root/test/rv64i/memory.txt" -t20000 "$elf" >"$out" 2>&1 || true
    if python3 - "$out" <<'PY'
import pathlib,sys
p=pathlib.Path(sys.argv[1]).read_bytes()
marker=b'DEBUG_FUNC_FT_LOG_SIZE=1024\n'
raise SystemExit(0 if p.partition(marker)[2][:1] == b'\x01' else 1)
PY
    then echo "PASS $(basename "$elf")"
    else echo "FAIL $(basename "$elf")"; status=1
    fi
    rm -f "$out"
  done
done
exit "$status"
