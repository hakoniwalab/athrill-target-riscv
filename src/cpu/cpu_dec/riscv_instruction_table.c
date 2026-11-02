/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Generated from riscv-opcodes commit f24ac23ec9fa947011781a09cd6fd25a7c36dc45.
 * Command: tools/generate_riscv_opcodes.py /path/to/riscv-opcodes
 *
 * Upstream license:
 * Copyright (c) 2022 RISC-V International
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "riscv_decode.h"

const RiscvDispatchEntry riscv_instruction_table[] = {
	{
		0x7fU, 0x37U,
		RISCV_OPERAND_RD, RISCV_IMMEDIATE_U,
		op_exec_lui, "lui", "rv_i"
	},
	{
		0x7fU, 0x17U,
		RISCV_OPERAND_RD, RISCV_IMMEDIATE_U,
		op_exec_auipc, "auipc", "rv_i"
	},
	{
		0x7fU, 0x6fU,
		RISCV_OPERAND_RD, RISCV_IMMEDIATE_J,
		op_exec_jal, "jal", "rv_i"
	},
	{
		0x707fU, 0x67U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_jalr, "jalr", "rv_i"
	},
	{
		0x707fU, 0x63U,
		RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_B,
		op_exec_beq, "beq", "rv_i"
	},
	{
		0x707fU, 0x1063U,
		RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_B,
		op_exec_bne, "bne", "rv_i"
	},
	{
		0x707fU, 0x4063U,
		RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_B,
		op_exec_blt, "blt", "rv_i"
	},
	{
		0x707fU, 0x5063U,
		RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_B,
		op_exec_bge, "bge", "rv_i"
	},
	{
		0x707fU, 0x6063U,
		RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_B,
		op_exec_bltu, "bltu", "rv_i"
	},
	{
		0x707fU, 0x7063U,
		RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_B,
		op_exec_bgeu, "bgeu", "rv_i"
	},
	{
		0x707fU, 0x3U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_lb, "lb", "rv_i"
	},
	{
		0x707fU, 0x1003U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_lh, "lh", "rv_i"
	},
	{
		0x707fU, 0x2003U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_lw, "lw", "rv_i"
	},
	{
		0x707fU, 0x4003U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_lbu, "lbu", "rv_i"
	},
	{
		0x707fU, 0x5003U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_lhu, "lhu", "rv_i"
	},
	{
		0x707fU, 0x6003U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_lwu, "lwu", "rv64_i"
	},
	{
		0x707fU, 0x3003U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_ld, "ld", "rv64_i"
	},
	{
		0x707fU, 0x23U,
		RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_S,
		op_exec_sb, "sb", "rv_i"
	},
	{
		0x707fU, 0x1023U,
		RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_S,
		op_exec_sh, "sh", "rv_i"
	},
	{
		0x707fU, 0x2023U,
		RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_S,
		op_exec_sw, "sw", "rv_i"
	},
	{
		0x707fU, 0x3023U,
		RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_S,
		op_exec_sd, "sd", "rv64_i"
	},
	{
		0x707fU, 0x13U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_addi, "addi", "rv_i"
	},
	{
		0x707fU, 0x2013U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_slti, "slti", "rv_i"
	},
	{
		0x707fU, 0x3013U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_sltiu, "sltiu", "rv_i"
	},
	{
		0x707fU, 0x4013U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_xori, "xori", "rv_i"
	},
	{
		0x707fU, 0x6013U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_ori, "ori", "rv_i"
	},
	{
		0x707fU, 0x7013U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_andi, "andi", "rv_i"
	},
	{
		0xfc00707fU, 0x1013U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_slli, "slli", "rv64_i"
	},
	{
		0xfc00707fU, 0x5013U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_srli, "srli", "rv64_i"
	},
	{
		0xfc00707fU, 0x40005013U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_srai, "srai", "rv64_i"
	},
	{
		0xfe00707fU, 0x33U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_add, "add", "rv_i"
	},
	{
		0xfe00707fU, 0x40000033U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_sub, "sub", "rv_i"
	},
	{
		0xfe00707fU, 0x1033U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_sll, "sll", "rv_i"
	},
	{
		0xfe00707fU, 0x2033U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_slt, "slt", "rv_i"
	},
	{
		0xfe00707fU, 0x3033U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_sltu, "sltu", "rv_i"
	},
	{
		0xfe00707fU, 0x4033U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_xor, "xor", "rv_i"
	},
	{
		0xfe00707fU, 0x5033U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_srl, "srl", "rv_i"
	},
	{
		0xfe00707fU, 0x40005033U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_sra, "sra", "rv_i"
	},
	{
		0xfe00707fU, 0x6033U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_or, "or", "rv_i"
	},
	{
		0xfe00707fU, 0x7033U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_and, "and", "rv_i"
	},
	{
		0xfe00707fU, 0x2000033U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_mul, "mul", "rv_m"
	},
	{
		0xfe00707fU, 0x2001033U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_mulh, "mulh", "rv_m"
	},
	{
		0xfe00707fU, 0x2002033U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_mulhsu, "mulhsu", "rv_m"
	},
	{
		0xfe00707fU, 0x2003033U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_mulhu, "mulhu", "rv_m"
	},
	{
		0xfe00707fU, 0x2004033U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_div, "div", "rv_m"
	},
	{
		0xfe00707fU, 0x2005033U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_divu, "divu", "rv_m"
	},
	{
		0xfe00707fU, 0x2006033U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_rem, "rem", "rv_m"
	},
	{
		0xfe00707fU, 0x2007033U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_remu, "remu", "rv_m"
	},
	{
		0x707fU, 0x1bU,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_addiw, "addiw", "rv64_i"
	},
	{
		0xfe00707fU, 0x101bU,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_slliw, "slliw", "rv64_i"
	},
	{
		0xfe00707fU, 0x501bU,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_srliw, "srliw", "rv64_i"
	},
	{
		0xfe00707fU, 0x4000501bU,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_sraiw, "sraiw", "rv64_i"
	},
	{
		0xfe00707fU, 0x3bU,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_addw, "addw", "rv64_i"
	},
	{
		0xfe00707fU, 0x4000003bU,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_subw, "subw", "rv64_i"
	},
	{
		0xfe00707fU, 0x103bU,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_sllw, "sllw", "rv64_i"
	},
	{
		0xfe00707fU, 0x503bU,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_srlw, "srlw", "rv64_i"
	},
	{
		0xfe00707fU, 0x4000503bU,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_sraw, "sraw", "rv64_i"
	},
	{
		0xfe00707fU, 0x200003bU,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_mulw, "mulw", "rv64_m"
	},
	{
		0xfe00707fU, 0x200403bU,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_divw, "divw", "rv64_m"
	},
	{
		0xfe00707fU, 0x200503bU,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_divuw, "divuw", "rv64_m"
	},
	{
		0xfe00707fU, 0x200603bU,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_remw, "remw", "rv64_m"
	},
	{
		0xfe00707fU, 0x200703bU,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_RS2, RISCV_IMMEDIATE_NONE,
		op_exec_remuw, "remuw", "rv64_m"
	},
	{
		0x707fU, 0xfU,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_FENCE, RISCV_IMMEDIATE_NONE,
		op_exec_fence, "fence", "rv_i"
	},
	{
		0x707fU, 0x100fU,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1, RISCV_IMMEDIATE_I,
		op_exec_fence_i, "fence.i", "rv_zifencei"
	},
	{
		0xffffffffU, 0x73U,
		0U, RISCV_IMMEDIATE_NONE,
		op_exec_ecall, "ecall", "rv_i"
	},
	{
		0xffffffffU, 0x100073U,
		0U, RISCV_IMMEDIATE_NONE,
		op_exec_ebreak, "ebreak", "rv_i"
	},
	{
		0xffffffffU, 0x30200073U,
		0U, RISCV_IMMEDIATE_NONE,
		op_exec_mret, "mret", "rv_system"
	},
	{
		0xffffffffU, 0x10500073U,
		0U, RISCV_IMMEDIATE_NONE,
		op_exec_wfi, "wfi", "rv_system"
	},
	{
		0x707fU, 0x1073U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_CSR, RISCV_IMMEDIATE_NONE,
		op_exec_csrrw, "csrrw", "rv_zicsr"
	},
	{
		0x707fU, 0x2073U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_CSR, RISCV_IMMEDIATE_NONE,
		op_exec_csrrs, "csrrs", "rv_zicsr"
	},
	{
		0x707fU, 0x3073U,
		RISCV_OPERAND_RD | RISCV_OPERAND_RS1 | RISCV_OPERAND_CSR, RISCV_IMMEDIATE_NONE,
		op_exec_csrrc, "csrrc", "rv_zicsr"
	},
	{
		0x707fU, 0x5073U,
		RISCV_OPERAND_RD | RISCV_OPERAND_CSR, RISCV_IMMEDIATE_NONE,
		op_exec_csrrwi, "csrrwi", "rv_zicsr"
	},
	{
		0x707fU, 0x6073U,
		RISCV_OPERAND_RD | RISCV_OPERAND_CSR, RISCV_IMMEDIATE_NONE,
		op_exec_csrrsi, "csrrsi", "rv_zicsr"
	},
	{
		0x707fU, 0x7073U,
		RISCV_OPERAND_RD | RISCV_OPERAND_CSR, RISCV_IMMEDIATE_NONE,
		op_exec_csrrci, "csrrci", "rv_zicsr"
	}
};

const uint32 riscv_instruction_table_count =
	(uint32)(sizeof(riscv_instruction_table) /
		sizeof(riscv_instruction_table[0]));
