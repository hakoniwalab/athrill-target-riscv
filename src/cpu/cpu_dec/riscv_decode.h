#ifndef ATHRILL_RISCV_DECODE_H
#define ATHRILL_RISCV_DECODE_H

#include "op_exec.h"

typedef enum {
	RISCV_IMMEDIATE_NONE,
	RISCV_IMMEDIATE_I,
	RISCV_IMMEDIATE_S,
	RISCV_IMMEDIATE_B,
	RISCV_IMMEDIATE_U,
	RISCV_IMMEDIATE_J
} RiscvImmediateKind;

enum {
	RISCV_OPERAND_RD = 1U << 0,
	RISCV_OPERAND_RS1 = 1U << 1,
	RISCV_OPERAND_RS2 = 1U << 2,
	RISCV_OPERAND_CSR = 1U << 3,
	RISCV_OPERAND_ZIMM = 1U << 4,
	RISCV_OPERAND_FENCE = 1U << 5
};

typedef struct {
	uint32 mask;
	uint32 match;
	uint16 operands;
	uint8 immediate_kind;
	RiscvDecodedExecutor execute;
	const char *mnemonic;
	const char *extension;
} RiscvDispatchEntry;

extern const RiscvDispatchEntry riscv_instruction_table[];
extern const uint32 riscv_instruction_table_count;

void riscv_decode_init(void);
const RiscvDispatchEntry *riscv_decode(uint32 instruction);
std_bool riscv_decode_fill(
	uint64 pc,
	uint32 instruction,
	uint8 length,
	RiscvDecodedInsn *decoded);
std_bool riscv_expand_compressed(uint16 instruction, uint32 *expanded);
void riscv_decode_cache_invalidate(
	TargetCoreType *core,
	uint64 address,
	uint32 size);

#endif
