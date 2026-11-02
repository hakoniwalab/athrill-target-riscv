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
#ifndef RISCV_ENCODING_H
#define RISCV_ENCODING_H

#define MATCH_ADD 0x33
#define MASK_ADD 0xfe00707f
#define MATCH_ADDI 0x13
#define MASK_ADDI 0x707f
#define MATCH_ADDIW 0x1b
#define MASK_ADDIW 0x707f
#define MATCH_ADDW 0x3b
#define MASK_ADDW 0xfe00707f
#define MATCH_AND 0x7033
#define MASK_AND 0xfe00707f
#define MATCH_ANDI 0x7013
#define MASK_ANDI 0x707f
#define MATCH_AUIPC 0x17
#define MASK_AUIPC 0x7f
#define MATCH_BEQ 0x63
#define MASK_BEQ 0x707f
#define MATCH_BGE 0x5063
#define MASK_BGE 0x707f
#define MATCH_BGEU 0x7063
#define MASK_BGEU 0x707f
#define MATCH_BLT 0x4063
#define MASK_BLT 0x707f
#define MATCH_BLTU 0x6063
#define MASK_BLTU 0x707f
#define MATCH_BNE 0x1063
#define MASK_BNE 0x707f
#define MATCH_C_ADD 0x9002
#define MASK_C_ADD 0xf003
#define MATCH_C_ADDI 0x1
#define MASK_C_ADDI 0xe003
#define MATCH_C_ADDI16SP 0x6101
#define MASK_C_ADDI16SP 0xef83
#define MATCH_C_ADDI4SPN 0x0
#define MASK_C_ADDI4SPN 0xe003
#define MATCH_C_ADDIW 0x2001
#define MASK_C_ADDIW 0xe003
#define MATCH_C_ADDW 0x9c21
#define MASK_C_ADDW 0xfc63
#define MATCH_C_AND 0x8c61
#define MASK_C_AND 0xfc63
#define MATCH_C_ANDI 0x8801
#define MASK_C_ANDI 0xec03
#define MATCH_C_BEQZ 0xc001
#define MASK_C_BEQZ 0xe003
#define MATCH_C_BNEZ 0xe001
#define MASK_C_BNEZ 0xe003
#define MATCH_C_EBREAK 0x9002
#define MASK_C_EBREAK 0xffff
#define MATCH_C_J 0xa001
#define MASK_C_J 0xe003
#define MATCH_C_JALR 0x9002
#define MASK_C_JALR 0xf07f
#define MATCH_C_JR 0x8002
#define MASK_C_JR 0xf07f
#define MATCH_C_LD 0x6000
#define MASK_C_LD 0xe003
#define MATCH_C_LDSP 0x6002
#define MASK_C_LDSP 0xe003
#define MATCH_C_LI 0x4001
#define MASK_C_LI 0xe003
#define MATCH_C_LUI 0x6001
#define MASK_C_LUI 0xe003
#define MATCH_C_LW 0x4000
#define MASK_C_LW 0xe003
#define MATCH_C_LWSP 0x4002
#define MASK_C_LWSP 0xe003
#define MATCH_C_MV 0x8002
#define MASK_C_MV 0xf003
#define MATCH_C_NOP 0x1
#define MASK_C_NOP 0xef83
#define MATCH_C_OR 0x8c41
#define MASK_C_OR 0xfc63
#define MATCH_C_SD 0xe000
#define MASK_C_SD 0xe003
#define MATCH_C_SDSP 0xe002
#define MASK_C_SDSP 0xe003
#define MATCH_C_SLLI 0x2
#define MASK_C_SLLI 0xe003
#define MATCH_C_SRAI 0x8401
#define MASK_C_SRAI 0xec03
#define MATCH_C_SRLI 0x8001
#define MASK_C_SRLI 0xec03
#define MATCH_C_SUB 0x8c01
#define MASK_C_SUB 0xfc63
#define MATCH_C_SUBW 0x9c01
#define MASK_C_SUBW 0xfc63
#define MATCH_C_SW 0xc000
#define MASK_C_SW 0xe003
#define MATCH_C_SWSP 0xc002
#define MASK_C_SWSP 0xe003
#define MATCH_C_XOR 0x8c21
#define MASK_C_XOR 0xfc63
#define MATCH_CSRRC 0x3073
#define MASK_CSRRC 0x707f
#define MATCH_CSRRCI 0x7073
#define MASK_CSRRCI 0x707f
#define MATCH_CSRRS 0x2073
#define MASK_CSRRS 0x707f
#define MATCH_CSRRSI 0x6073
#define MASK_CSRRSI 0x707f
#define MATCH_CSRRW 0x1073
#define MASK_CSRRW 0x707f
#define MATCH_CSRRWI 0x5073
#define MASK_CSRRWI 0x707f
#define MATCH_DIV 0x2004033
#define MASK_DIV 0xfe00707f
#define MATCH_DIVU 0x2005033
#define MASK_DIVU 0xfe00707f
#define MATCH_DIVUW 0x200503b
#define MASK_DIVUW 0xfe00707f
#define MATCH_DIVW 0x200403b
#define MASK_DIVW 0xfe00707f
#define MATCH_EBREAK 0x100073
#define MASK_EBREAK 0xffffffff
#define MATCH_ECALL 0x73
#define MASK_ECALL 0xffffffff
#define MATCH_FADD_S 0x53
#define MASK_FADD_S 0xfe00007f
#define MATCH_FCLASS_S 0xe0001053
#define MASK_FCLASS_S 0xfff0707f
#define MATCH_FCVT_L_S 0xc0200053
#define MASK_FCVT_L_S 0xfff0007f
#define MATCH_FCVT_LU_S 0xc0300053
#define MASK_FCVT_LU_S 0xfff0007f
#define MATCH_FCVT_S_L 0xd0200053
#define MASK_FCVT_S_L 0xfff0007f
#define MATCH_FCVT_S_LU 0xd0300053
#define MASK_FCVT_S_LU 0xfff0007f
#define MATCH_FCVT_S_W 0xd0000053
#define MASK_FCVT_S_W 0xfff0007f
#define MATCH_FCVT_S_WU 0xd0100053
#define MASK_FCVT_S_WU 0xfff0007f
#define MATCH_FCVT_W_S 0xc0000053
#define MASK_FCVT_W_S 0xfff0007f
#define MATCH_FCVT_WU_S 0xc0100053
#define MASK_FCVT_WU_S 0xfff0007f
#define MATCH_FDIV_S 0x18000053
#define MASK_FDIV_S 0xfe00007f
#define MATCH_FENCE 0xf
#define MASK_FENCE 0x707f
#define MATCH_FENCE_I 0x100f
#define MASK_FENCE_I 0x707f
#define MATCH_FEQ_S 0xa0002053
#define MASK_FEQ_S 0xfe00707f
#define MATCH_FLE_S 0xa0000053
#define MASK_FLE_S 0xfe00707f
#define MATCH_FLT_S 0xa0001053
#define MASK_FLT_S 0xfe00707f
#define MATCH_FLW 0x2007
#define MASK_FLW 0x707f
#define MATCH_FMADD_S 0x43
#define MASK_FMADD_S 0x600007f
#define MATCH_FMAX_S 0x28001053
#define MASK_FMAX_S 0xfe00707f
#define MATCH_FMIN_S 0x28000053
#define MASK_FMIN_S 0xfe00707f
#define MATCH_FMSUB_S 0x47
#define MASK_FMSUB_S 0x600007f
#define MATCH_FMUL_S 0x10000053
#define MASK_FMUL_S 0xfe00007f
#define MATCH_FMV_W_X 0xf0000053
#define MASK_FMV_W_X 0xfff0707f
#define MATCH_FMV_X_W 0xe0000053
#define MASK_FMV_X_W 0xfff0707f
#define MATCH_FNMADD_S 0x4f
#define MASK_FNMADD_S 0x600007f
#define MATCH_FNMSUB_S 0x4b
#define MASK_FNMSUB_S 0x600007f
#define MATCH_FSGNJ_S 0x20000053
#define MASK_FSGNJ_S 0xfe00707f
#define MATCH_FSGNJN_S 0x20001053
#define MASK_FSGNJN_S 0xfe00707f
#define MATCH_FSGNJX_S 0x20002053
#define MASK_FSGNJX_S 0xfe00707f
#define MATCH_FSQRT_S 0x58000053
#define MASK_FSQRT_S 0xfff0007f
#define MATCH_FSUB_S 0x8000053
#define MASK_FSUB_S 0xfe00007f
#define MATCH_FSW 0x2027
#define MASK_FSW 0x707f
#define MATCH_JAL 0x6f
#define MASK_JAL 0x7f
#define MATCH_JALR 0x67
#define MASK_JALR 0x707f
#define MATCH_LB 0x3
#define MASK_LB 0x707f
#define MATCH_LBU 0x4003
#define MASK_LBU 0x707f
#define MATCH_LD 0x3003
#define MASK_LD 0x707f
#define MATCH_LH 0x1003
#define MASK_LH 0x707f
#define MATCH_LHU 0x5003
#define MASK_LHU 0x707f
#define MATCH_LUI 0x37
#define MASK_LUI 0x7f
#define MATCH_LW 0x2003
#define MASK_LW 0x707f
#define MATCH_LWU 0x6003
#define MASK_LWU 0x707f
#define MATCH_MRET 0x30200073
#define MASK_MRET 0xffffffff
#define MATCH_MUL 0x2000033
#define MASK_MUL 0xfe00707f
#define MATCH_MULH 0x2001033
#define MASK_MULH 0xfe00707f
#define MATCH_MULHSU 0x2002033
#define MASK_MULHSU 0xfe00707f
#define MATCH_MULHU 0x2003033
#define MASK_MULHU 0xfe00707f
#define MATCH_MULW 0x200003b
#define MASK_MULW 0xfe00707f
#define MATCH_OR 0x6033
#define MASK_OR 0xfe00707f
#define MATCH_ORI 0x6013
#define MASK_ORI 0x707f
#define MATCH_PAUSE 0x100000f
#define MASK_PAUSE 0xffffffff
#define MATCH_REM 0x2006033
#define MASK_REM 0xfe00707f
#define MATCH_REMU 0x2007033
#define MASK_REMU 0xfe00707f
#define MATCH_REMUW 0x200703b
#define MASK_REMUW 0xfe00707f
#define MATCH_REMW 0x200603b
#define MASK_REMW 0xfe00707f
#define MATCH_SB 0x23
#define MASK_SB 0x707f
#define MATCH_SD 0x3023
#define MASK_SD 0x707f
#define MATCH_SH 0x1023
#define MASK_SH 0x707f
#define MATCH_SLL 0x1033
#define MASK_SLL 0xfe00707f
#define MATCH_SLLI 0x1013
#define MASK_SLLI 0xfc00707f
#define MATCH_SLLIW 0x101b
#define MASK_SLLIW 0xfe00707f
#define MATCH_SLLW 0x103b
#define MASK_SLLW 0xfe00707f
#define MATCH_SLT 0x2033
#define MASK_SLT 0xfe00707f
#define MATCH_SLTI 0x2013
#define MASK_SLTI 0x707f
#define MATCH_SLTIU 0x3013
#define MASK_SLTIU 0x707f
#define MATCH_SLTU 0x3033
#define MASK_SLTU 0xfe00707f
#define MATCH_SRA 0x40005033
#define MASK_SRA 0xfe00707f
#define MATCH_SRAI 0x40005013
#define MASK_SRAI 0xfc00707f
#define MATCH_SRAIW 0x4000501b
#define MASK_SRAIW 0xfe00707f
#define MATCH_SRAW 0x4000503b
#define MASK_SRAW 0xfe00707f
#define MATCH_SRL 0x5033
#define MASK_SRL 0xfe00707f
#define MATCH_SRLI 0x5013
#define MASK_SRLI 0xfc00707f
#define MATCH_SRLIW 0x501b
#define MASK_SRLIW 0xfe00707f
#define MATCH_SRLW 0x503b
#define MASK_SRLW 0xfe00707f
#define MATCH_SUB 0x40000033
#define MASK_SUB 0xfe00707f
#define MATCH_SUBW 0x4000003b
#define MASK_SUBW 0xfe00707f
#define MATCH_SW 0x2023
#define MASK_SW 0x707f
#define MATCH_WFI 0x10500073
#define MASK_WFI 0xffffffff
#define MATCH_XOR 0x4033
#define MASK_XOR 0xfe00707f
#define MATCH_XORI 0x4013
#define MASK_XORI 0x707f

#endif
