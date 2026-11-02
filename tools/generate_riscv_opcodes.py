#!/usr/bin/env python3
"""Generate the target's mask/match header from riscv-opcodes.

Usage: tools/generate_riscv_opcodes.py /path/to/riscv-opcodes
Pinned upstream commit: f24ac23ec9fa947011781a09cd6fd25a7c36dc45
"""
import pathlib
import re
import subprocess
import sys

COMMIT = "f24ac23ec9fa947011781a09cd6fd25a7c36dc45"
EXTENSIONS = ["rv_i", "rv64_i", "rv_m", "rv64_m", "rv_f", "rv64_f",
              "rv_c", "rv64_c", "rv_zicsr", "rv_zifencei", "rv_system"]

root = pathlib.Path(sys.argv[1]).resolve()
actual = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
if actual != COMMIT:
    raise SystemExit(f"expected riscv-opcodes {COMMIT}, got {actual}")
subprocess.run([sys.executable, "-c", "from riscv_opcodes.parse import main; main()",
                "-c", *EXTENSIONS], cwd=root, env={**__import__('os').environ,
                "PYTHONPATH": str(root / "src")}, check=True)
source = (root / "encoding.out.h").read_text()
definitions = []
for line in source.splitlines():
    if re.match(r"#define (MATCH|MASK)_[A-Z0-9_]+\s", line):
        definitions.append(line.replace("_RISCV_UL(", "(").replace("_RISCV_ULL(", "("))

license_text = (root / "LICENSE").read_text().rstrip()
out = pathlib.Path(__file__).resolve().parents[1] / "src/cpu/decode/riscv_encoding.h"
out.write_text("/* SPDX-License-Identifier: BSD-3-Clause\n *\n" +
               " * Generated from riscv-opcodes commit " + COMMIT + ".\n" +
               " * Command: tools/generate_riscv_opcodes.py /path/to/riscv-opcodes\n" +
               " *\n * Upstream license:\n" +
               "\n".join(" * " + x for x in license_text.splitlines()) +
               "\n */\n#ifndef RISCV_ENCODING_H\n#define RISCV_ENCODING_H\n\n" +
               "\n".join(definitions) + "\n\n#endif\n")
