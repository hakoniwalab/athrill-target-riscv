#!/usr/bin/env python3
"""Report the first PC/instruction mismatch in Athrill and sysemu traces."""
import gzip
import re
import sys

ATHRILL = re.compile(r"I hart=.* pc=([0-9a-f]+) insn=([0-9a-f]+)")
SYSEMU = re.compile(r" I\([A-Z]\): 0x([0-9a-f]+) \(0x([0-9a-f]+)\)")


def records(path, pattern):
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt", errors="replace") as stream:
        for line in stream:
            match = pattern.search(line)
            if match:
                yield tuple(int(value, 16) for value in match.groups())


def main():
    left = records(sys.argv[1], ATHRILL)
    right = records(sys.argv[2], SYSEMU)
    index = 0
    while True:
        a = next(left, None)
        b = next(right, None)
        same = a == b
        if a is not None and b is not None and a[0] == b[0] and b[1] <= 0xffff:
            # Athrill internally expands C instructions before execution.
            same = True
        if not same:
            print(f"first mismatch record {index}: athrill={a} sysemu={b}")
            return 1
        if a is None:
            print(f"traces match through {index} records")
            return 0
        index += 1


if __name__ == "__main__":
    raise SystemExit(main())
