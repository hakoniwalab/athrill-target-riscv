#!/usr/bin/env python3
"""Generate decoder metadata from the pinned riscv-opcodes checkout.

Usage: tools/generate_riscv_opcodes.py /path/to/riscv-opcodes
Pinned upstream commit: f24ac23ec9fa947011781a09cd6fd25a7c36dc45
"""
import json
import os
import pathlib
import re
import subprocess
import sys

COMMIT = "f24ac23ec9fa947011781a09cd6fd25a7c36dc45"
EXTENSIONS = ["rv_i", "rv64_i", "rv_m", "rv64_m", "rv_f", "rv64_f",
              "rv_c", "rv64_c", "rv_zicsr", "rv_zifencei", "rv_system"]
ENABLED = """lui auipc jal jalr beq bne blt bge bltu bgeu lb lh lw lbu lhu
lwu ld sb sh sw sd addi slti sltiu xori ori andi slli srli srai add sub sll
slt sltu xor srl sra or and mul mulh mulhsu mulhu div divu rem remu addiw
slliw srliw sraiw addw subw sllw srlw sraw mulw divw divuw remw remuw fence
fence_i ecall ebreak mret wfi csrrw csrrs csrrc csrrwi csrrsi csrrci
flw fsw fmadd_s fmsub_s fnmsub_s fnmadd_s fadd_s fsub_s fmul_s fdiv_s
fsqrt_s fsgnj_s fsgnjn_s fsgnjx_s fmin_s fmax_s fcvt_w_s fcvt_wu_s
fmv_x_w feq_s flt_s fle_s fclass_s fcvt_s_w fcvt_s_wu fmv_w_x
fcvt_l_s fcvt_lu_s fcvt_s_l fcvt_s_lu""".split()


def notice(license_text):
    return ("/* SPDX-License-Identifier: BSD-3-Clause\n *\n"
            f" * Generated from riscv-opcodes commit {COMMIT}.\n"
            " * Command: tools/generate_riscv_opcodes.py /path/to/riscv-opcodes\n"
            " *\n * Upstream license:\n" +
            "\n".join(" * " + line for line in license_text.splitlines()) +
            "\n */\n")


def operands(fields):
    flags = []
    for field, flag in (("rd", "RISCV_OPERAND_RD"),
                        ("rs1", "RISCV_OPERAND_RS1"),
                        ("rs2", "RISCV_OPERAND_RS2"),
                        ("csr", "RISCV_OPERAND_CSR"),
                        ("zimm", "RISCV_OPERAND_ZIMM")):
        if field in fields:
            flags.append(flag)
    if any(field in fields for field in ("fm", "pred", "succ")):
        flags.append("RISCV_OPERAND_FENCE")
    return " | ".join(flags) if flags else "0U"


def immediate(fields):
    if any(field in fields for field in ("imm12", "shamtd", "shamtw")):
        return "RISCV_IMMEDIATE_I"
    if "imm12hi" in fields:
        return "RISCV_IMMEDIATE_S"
    if "bimm12hi" in fields:
        return "RISCV_IMMEDIATE_B"
    if "imm20" in fields:
        return "RISCV_IMMEDIATE_U"
    if "jimm20" in fields:
        return "RISCV_IMMEDIATE_J"
    return "RISCV_IMMEDIATE_NONE"


def write_table(path, instructions, header):
    lines = [header, '#include "riscv_decode.h"\n\n',
             "const RiscvDispatchEntry riscv_instruction_table[] = {\n"]
    entries = []
    for mnemonic in ENABLED:
        data = instructions[mnemonic]
        function = mnemonic.replace(".", "_")
        display_name = mnemonic.replace("_", ".")
        fields = data["variable_fields"]
        entries.append(
            "\t{\n"
            f"\t\t{data['mask']}U, {data['match']}U,\n"
            f"\t\t{operands(fields)}, {immediate(fields)},\n"
            f"\t\top_exec_{function}, \"{display_name}\", "
            f"\"{data['extension'][0]}\"\n\t}}")
    lines.append(",\n".join(entries))
    lines += ["\n};\n\n", "const uint32 riscv_instruction_table_count =\n",
              "\t(uint32)(sizeof(riscv_instruction_table) /\n",
              "\t\tsizeof(riscv_instruction_table[0]));\n"]
    path.write_text("".join(lines))


def main():
    root = pathlib.Path(sys.argv[1]).resolve()
    actual = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if actual != COMMIT:
        raise SystemExit(f"expected riscv-opcodes {COMMIT}, got {actual}")
    subprocess.run(
        [sys.executable, "-c", "from riscv_opcodes.parse import main; main()",
         "-c", *EXTENSIONS], cwd=root,
        env={**os.environ, "PYTHONPATH": str(root / "src")}, check=True)
    destination = pathlib.Path(__file__).resolve().parents[1] / "src/cpu/cpu_dec"
    header = notice((root / "LICENSE").read_text().rstrip())
    definitions = []
    for line in (root / "encoding.out.h").read_text().splitlines():
        if re.match(r"#define (MATCH|MASK)_[A-Z0-9_]+\s", line):
            definitions.append(line.replace("_RISCV_UL(", "(").replace(
                "_RISCV_ULL(", "("))
    (destination / "riscv_encoding.h").write_text(
        header + "#ifndef RISCV_ENCODING_H\n#define RISCV_ENCODING_H\n\n" +
        "\n".join(definitions) + "\n\n#endif\n")
    instructions = json.loads((root / "instr_dict.json").read_text())
    write_table(destination / "riscv_instruction_table.c", instructions, header)


if __name__ == "__main__":
    main()
