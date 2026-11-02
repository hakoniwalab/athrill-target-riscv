#!/usr/bin/env python3
"""Compare control flow and architectural deltas in Athrill/sysemu traces."""
import gzip
import re
import sys

ATHRILL_INSN = re.compile(r"I hart=\d+ .*pc=([0-9a-f]+) insn=([0-9a-f]+)")
ATHRILL_DELTA = re.compile(r"([XFCW]) hart=\d+ (?:reg|csr|addr)=([0-9a-f]+) (?:size=(\d+) )?value=([0-9a-f]+)")
SYSEMU_INSN = re.compile(r" I\([A-Z]\): 0x([0-9a-f]+) \(0x([0-9a-f]+)\)")
SYSEMU_REG = re.compile(r"\s([xf])(\d+) = 0x([0-9a-f]+)")
SYSEMU_FVECTOR = re.compile(r"\sf(\d+) = \{ 0:0x([0-9a-f]+)")
SYSEMU_MEMORY = re.compile(r"MEM(8|16|32|64)\[0x([0-9a-f]+)\] = 0x([0-9a-f]+)")
SYSEMU_CSR = re.compile(r"\s([a-z][a-z0-9]*) = 0x([0-9a-f]+)")
SYSEMU_TRAP = re.compile(r"Trapping to [A-Z]-mode with cause 0x([0-9a-f]+) and tval 0x([0-9a-f]+)")

CSR_NUMBERS = {
    "fflags": 0x001, "frm": 0x002, "fcsr": 0x003,
    "sstatus": 0x100, "sie": 0x104, "stvec": 0x105,
    "scounteren": 0x106, "sscratch": 0x140, "sepc": 0x141,
    "scause": 0x142, "stval": 0x143, "sip": 0x144, "satp": 0x180,
    "mstatus": 0x300, "misa": 0x301, "medeleg": 0x302,
    "mideleg": 0x303, "mie": 0x304, "mtvec": 0x305,
    "mcounteren": 0x306, "menvcfg": 0x30A, "mscratch": 0x340,
    "mepc": 0x341, "mcause": 0x342, "mtval": 0x343, "mip": 0x344,
}


def lines(path):
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt", errors="replace") as stream:
        yield from stream


def athrill_records(path):
    record = None
    state = {}
    for line in lines(path):
        instruction = ATHRILL_INSN.search(line)
        if instruction:
            if record is not None:
                yield record
            record = [int(value, 16) for value in instruction.groups()] + [{}]
            continue
        delta = ATHRILL_DELTA.search(line)
        if record is None or delta is None:
            continue
        kind, target, size, value = delta.groups()
        target = int(target, 16 if kind in "CW" else 10)
        key = (kind, target, int(size) if size else 0)
        value = int(value, 16)
        if kind == "F":
            value &= 0xFFFFFFFF
        if kind == "W" or state.get(key, 0) != value:
            record[2][key] = value
        state[key] = value
    if record is not None:
        yield record


def sysemu_records(path):
    record = None
    state = {("C", 0x300, 0): 0x0000000A00001800}
    for line in lines(path):
        instruction = SYSEMU_INSN.search(line)
        if instruction:
            if record is not None:
                yield record
            record = [int(value, 16) for value in instruction.groups()] + [{}]
            if record[1] == 0x30200073:
                key = ("C", 0x300, 0)
                status = state.get(key, 0)
                status = (status & ~(1 << 3)) | (((status >> 7) & 1) << 3)
                status |= 1 << 7
                status &= ~(3 << 11)
                if state.get(key, 0) != status:
                    record[2][key] = status
                state[key] = status
            continue
        if record is None:
            continue
        match = SYSEMU_REG.search(line)
        if match:
            kind, target, value = match.groups()
            key = ("X" if kind == "x" else "F", int(target), 0)
            value = int(value, 16)
            if key[1] != 0 and state.get(key, 0) != value:
                record[2][key] = value
            state[key] = value
            continue
        match = SYSEMU_FVECTOR.search(line)
        if match:
            target, value = match.groups()
            key = ("F", int(target), 0)
            value = int(value, 16)
            if state.get(key, 0) != value:
                record[2][key] = value
            state[key] = value
            continue
        match = SYSEMU_MEMORY.search(line)
        if match:
            bits, address, value = match.groups()
            record[2][("W", int(address, 16), int(bits) // 8)] = int(value, 16)
            continue
        match = SYSEMU_CSR.search(line)
        if match and match.group(1) in CSR_NUMBERS:
            name, value = match.groups()
            key = ("C", CSR_NUMBERS[name], 0)
            value = int(value, 16)
            if state.get(key, 0) != value:
                record[2][key] = value
            state[key] = value
            continue
        match = SYSEMU_TRAP.search(line)
        if match:
            cause, tval = (int(value, 16) for value in match.groups())
            status_key = ('C', 0x300, 0)
            status = state.get(status_key, 0)
            status = (status & ~(3 << 11)) | (3 << 11)
            status = (status & ~(1 << 7)) | (((status >> 3) & 1) << 7)
            status &= ~(1 << 3)
            for key, value in ((('C', 0x341, 0), record[0]),
                               (('C', 0x342, 0), cause),
                               (('C', 0x343, 0), tval),
                               (status_key, status)):
                if state.get(key, 0) != value:
                    record[2][key] = value
                state[key] = value
    if record is not None:
        yield record


def main():
    left = athrill_records(sys.argv[1])
    right = sysemu_records(sys.argv[2])
    index = 0
    while True:
        athrill = next(left, None)
        sysemu = next(right, None)
        if athrill is None or sysemu is None:
            if athrill == sysemu:
                print(f"traces match through {index} records")
                return 0
            print(f"trace ended at record {index}: athrill={athrill} sysemu={sysemu}")
            return 1
        same_instruction = athrill[:2] == sysemu[:2]
        if athrill[0] == sysemu[0] and sysemu[1] <= 0xFFFF:
            same_instruction = True
        if not same_instruction:
            print(f"control mismatch record {index}: athrill={athrill[:2]} sysemu={sysemu[:2]}")
            return 1
        if athrill[2] != sysemu[2]:
            keys = sorted(set(athrill[2]) | set(sysemu[2]))
            for key in keys:
                if athrill[2].get(key) != sysemu[2].get(key):
                    print(f"state mismatch record {index} pc=0x{athrill[0]:x} event={key}: athrill={athrill[2].get(key)} sysemu={sysemu[2].get(key)}")
            return 1
        index += 1


if __name__ == "__main__":
    raise SystemExit(main())
