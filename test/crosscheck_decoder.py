#!/usr/bin/env python3
"""Cross-check Athrill's decoder against GNU objdump (independent opcode tables).

GNU binutils keeps its own RISC-V opcode tables (opcodes/riscv-opc.c), so it
is an independent reference for the decoder generated from riscv-opcodes and
for the hand-written compressed expansion table.

  * 32-bit words: every enabled instruction is sampled with random operand
    bits, plus fully random words. Where Athrill decodes a word, objdump must
    name the same instruction; where objdump names an instruction Athrill
    enables, Athrill must decode it.
  * 16-bit words: all 49152 compressed encodings. Where Athrill expands a
    halfword, objdump's text for the halfword and for Athrill's 32-bit
    expansion must be equal (objdump prints compressed instructions with
    their base mnemonic and all operands); where objdump decodes a halfword
    of an extension Athrill implements, Athrill must expand it. HINT
    encodings (objdump prints them as c.nop or c.slli64/c.srli64... with
    rd = x0 or a zero shift) expand to instructions with no effect.

usage: crosscheck_decoder.py --dump build/athrill-decoder-dump
       [--image athrill-riscv-toolchain:ubuntu24] [--random 200000] [--seed 1]
"""
import argparse
import pathlib
import random
import re
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
OBJDUMP = "riscv64-unknown-elf-objdump"
# Compressed encodings of extensions Athrill does not implement (D, Zcb...).
NOT_IMPLEMENTED_COMPRESSED = re.compile(r"^(fld|fsd|fldsp|fsdsp|c\.fld|c\.fsd|c\.fldsp|c\.fsdsp)\b")


def enabled_instructions():
    """(name, mask, match) for each instruction in Athrill's decoder table."""
    table = (ROOT / "src/cpu/cpu_dec/riscv_instruction_table.c").read_text()
    entries = re.findall(r"\{\s*(0x[0-9a-fA-F]+)U,\s*(0x[0-9a-fA-F]+)U,[^}]*?op_exec_\w+,\s*\"([a-z0-9_.]+)\"",
                         table)
    return [(name, int(mask, 16), int(match, 16)) for mask, match, name in entries]


def objdump(image, words, size, aliases):
    """Disassemble the words (size 2 or 4 bytes) and return one text per word."""
    fmt = "<H" if size == 2 else "<I"
    with tempfile.TemporaryDirectory() as directory:
        path = pathlib.Path(directory) / "words.bin"
        path.write_bytes(b"".join(struct.pack(fmt, word) for word in words))
        options = [] if aliases else ["-M", "no-aliases"]
        output = subprocess.run(
            ["docker", "run", "--rm", "-v", f"{directory}:/data:ro", image,
             OBJDUMP, "-D", "-b", "binary", "-m", "riscv:rv64", *options, "/data/words.bin"],
            check=True, stdout=subprocess.PIPE, text=True).stdout
    texts = {}
    for line in output.splitlines():
        match = re.match(r"\s*([0-9a-f]+):\s+([0-9a-f]+)\s+(.*)$", line)
        if match:
            address = int(match.group(1), 16)
            text = match.group(3).strip()
            texts[address // size] = normalise(text, address) if aliases else text
    return [texts.get(index, "") for index in range(len(words))]


BRANCHES = {"j", "jal", "beq", "bne", "blt", "bge", "bltu", "bgeu",
            "beqz", "bnez", "blez", "bgez", "bltz", "bgtz", "bgt", "ble", "bgtu", "bleu"}
SAME_MEANING = [
    (r"^addi\s+(\w+),\1,0$", r"mv \1,\1"),
    (r"^add\s+(\w+),zero,(\w+)$", r"mv \1,\2"),
    (r"^c\.(slli|srli|srai)64\s+(\w+)$", r"\1 \2,\2,0"),
    (r",0x0$", ",0"),
]


def normalise(text, address):
    """Make objdump text independent of where the word was placed."""
    text = re.sub(r"\s+#.*$", "", text)
    text = re.sub(r"\s+", " ", text)
    parts = text.split(" ", 1)
    if parts[0] in BRANCHES and len(parts) == 2:
        operands = parts[1].split(",")
        target = int(operands[-1], 16)
        operands[-1] = f"pc{target - address:+d}"
        text = parts[0] + " " + ",".join(operands)
    for pattern, replacement in SAME_MEANING:
        text = re.sub(pattern, replacement, text)
    return text


def athrill(dump, kind, words):
    request = "".join(f"{kind} {word:x}\n" for word in words)
    output = subprocess.run([dump], input=request, check=True,
                            stdout=subprocess.PIPE, text=True).stdout
    return [line.split() for line in output.splitlines()]


def mnemonic(text):
    first = text.split()[0] if text else ""
    return "" if first.startswith(".") or first == "unimp" else first


def check_words(args, failures):
    generator = random.Random(args.seed)
    instructions = enabled_instructions()
    enabled = {name.replace("_", ".") for name, _, _ in instructions}
    words = []
    for _, mask, match in instructions:
        for _ in range(500):
            words.append(match | (generator.getrandbits(32) & ~mask & 0xffffffff))
    words += [generator.getrandbits(32) | 3 for _ in range(args.random)]
    longer = [word for word in words if (word & 0x1f) == 0x1f]
    words = [word for word in words if (word & 0x1f) != 0x1f]
    for _, word, name in athrill(args.dump, "w", longer):
        if name != "ILLEGAL":
            failures.append(f"w {word}: athrill {name}, but the word starts a longer instruction")
    ours = athrill(args.dump, "w", words)
    theirs = objdump(args.image, words, 4, aliases=False)
    checked = 0
    for (_, word, name), text in zip(ours, theirs):
        reference = mnemonic(text)
        mine = "" if name == "ILLEGAL" else name.replace("_", ".")
        if mine:
            checked += 1
            if mine in ("fence", "fence.i") and reference == "":
                continue
            if mine != reference:
                failures.append(f"w {word}: athrill {mine}, objdump '{text}'")
        elif reference in enabled:
            failures.append(f"w {word}: athrill ILLEGAL, objdump '{text}'")
    print(f"32-bit: {len(words)} words, {checked} decoded by Athrill")


# Base opcodes whose only architectural effect is writing rd: with rd = x0
# they change no state (HINT). OP-IMM (addi, slli, ...), OP (add, ...), LUI.
NO_EFFECT_WITH_RD_X0 = {0x13, 0x33, 0x37}
HINT_NAMES = {"c.nop", "c.addi", "c.li", "c.lui", "c.mv", "c.add", "c.slli", "c.slli64"}


def is_hint(row, plain):
    """A compressed HINT that Athrill expands to an instruction with no effect.

    objdump names it as a HINT form, and the 32-bit expansion is an
    instruction whose only effect is writing rd, with rd = x0.
    """
    expanded = int(row[2], 16)
    if plain.split()[0] not in HINT_NAMES:
        return False
    return ((expanded >> 7) & 31) == 0 and (expanded & 0x7f) in NO_EFFECT_WITH_RD_X0


def check_compressed(args, failures):
    halfwords = [value for value in range(0x10000) if (value & 3) != 3]
    ours = athrill(args.dump, "c", halfwords)
    compressed_text = objdump(args.image, halfwords, 2, aliases=True)
    compressed_plain = objdump(args.image, halfwords, 2, aliases=False)
    expanded = [int(row[2], 16) for row in ours if len(row) == 4]
    expanded_text = iter(objdump(args.image, expanded, 4, aliases=True))
    checked = 0
    for row, text, plain in zip(ours, compressed_text, compressed_plain):
        reference = mnemonic(text)
        if len(row) == 4:
            checked += 1
            mine = next(expanded_text)
            if mine != text and not is_hint(row, plain):
                failures.append(f"c {row[1]}: athrill expands to {row[2]} '{mine}', objdump '{text}'")
        elif plain.startswith("c.addi16sp") and plain.endswith(",0"):
            continue
        elif reference and not NOT_IMPLEMENTED_COMPRESSED.match(mnemonic(plain)):
            failures.append(f"c {row[1]}: athrill ILLEGAL, objdump '{text}' ('{plain}')")
    print(f"16-bit: {len(halfwords)} halfwords, {checked} expanded by Athrill")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", required=True)
    parser.add_argument("--image", default="athrill-riscv-toolchain:ubuntu24")
    parser.add_argument("--random", type=int, default=200000)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--show", type=int, default=40, help="mismatches to print")
    args = parser.parse_args()
    failures = []
    check_words(args, failures)
    check_compressed(args, failures)
    for failure in failures[:args.show]:
        print("FAIL " + failure)
    print(f"{len(failures)} mismatch(es)")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
