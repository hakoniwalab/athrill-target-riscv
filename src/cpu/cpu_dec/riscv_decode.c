#include "riscv_decode.h"

#define RISCV_OPCODE_COUNT 128U
#define RISCV_FUNCT3_COUNT 8U

static sint16 decode_heads[RISCV_OPCODE_COUNT][RISCV_FUNCT3_COUNT];
static sint16 decode_links[128];
static int decode_initialized;

static uint64 sign_extend(uint64 value, uint32 width)
{
	uint32 shift = 64U - width;

	return (uint64)((sint64)(value << shift) >> shift);
}

static uint64 decode_immediate(uint32 instruction, uint8 kind)
{
	uint64 value;

	switch (kind) {
	case RISCV_IMMEDIATE_I:
		value = instruction >> 20U;
		return sign_extend(value, 12U);
	case RISCV_IMMEDIATE_S:
		value = ((instruction >> 25U) << 5U);
		value |= (instruction >> 7U) & 31U;
		return sign_extend(value, 12U);
	case RISCV_IMMEDIATE_B:
		value = ((uint64)instruction >> 31U) << 12U;
		value |= ((instruction >> 7U) & 1U) << 11U;
		value |= ((instruction >> 25U) & 63U) << 5U;
		value |= ((instruction >> 8U) & 15U) << 1U;
		return sign_extend(value, 13U);
	case RISCV_IMMEDIATE_U:
		value = instruction & 0xfffff000U;
		return sign_extend(value, 32U);
	case RISCV_IMMEDIATE_J:
		value = ((uint64)instruction >> 31U) << 20U;
		value |= instruction & 0x000ff000U;
		value |= ((instruction >> 20U) & 1U) << 11U;
		value |= ((instruction >> 21U) & 1023U) << 1U;
		return sign_extend(value, 21U);
	default:
		return 0U;
	}
}

void riscv_decode_init(void)
{
	uint32 opcode;
	uint32 funct3;
	uint32 index;
	const RiscvDispatchEntry *entry;

	for (opcode = 0U; opcode < RISCV_OPCODE_COUNT; opcode++) {
		for (funct3 = 0U; funct3 < RISCV_FUNCT3_COUNT; funct3++) {
			decode_heads[opcode][funct3] = -1;
		}
	}
	for (index = 0U; index < riscv_instruction_table_count; index++) {
		entry = &riscv_instruction_table[index];
		opcode = entry->match & 127U;
		if ((entry->mask & 0x7000U) == 0U) {
			for (funct3 = 0U; funct3 < RISCV_FUNCT3_COUNT; funct3++) {
				decode_heads[opcode][funct3] = (sint16)(-2 - index);
			}
		}
		else {
			funct3 = (entry->match >> 12U) & 7U;
			decode_links[index] = decode_heads[opcode][funct3];
			decode_heads[opcode][funct3] = (sint16)index;
		}
	}
	decode_initialized = 1;
}

const RiscvDispatchEntry *riscv_decode(uint32 instruction)
{
	sint16 index;
	uint32 direct_index;
	const RiscvDispatchEntry *entry;

	if (!decode_initialized) {
		riscv_decode_init();
	}
	index = decode_heads[instruction & 127U][(instruction >> 12U) & 7U];
	if (index <= -2) {
		direct_index = (uint32)(-2 - index);
		entry = &riscv_instruction_table[direct_index];
		if ((instruction & entry->mask) == entry->match) {
			return entry;
		}
		return 0;
	}
	while (index >= 0) {
		entry = &riscv_instruction_table[index];
		if ((instruction & entry->mask) == entry->match) {
			return entry;
		}
		index = decode_links[index];
	}
	return 0;
}

static void select_specialisation(RiscvDecodedInsn *decoded)
{
	if (decoded->execute != op_exec_addi) {
		return;
	}
	if (decoded->rd == 0U) {
		decoded->execute = op_exec_discard;
	}
	else if (decoded->rs1 == 0U) {
		decoded->execute = op_exec_li;
	}
	else if (decoded->immediate == 0U) {
		decoded->execute = op_exec_mv;
	}
}

std_bool riscv_decode_fill(
	uint64 pc,
	uint32 instruction,
	uint8 length,
	RiscvDecodedInsn *decoded)
{
	const RiscvDispatchEntry *entry = riscv_decode(instruction);

	if (entry == 0) {
		return FALSE;
	}
	decoded->pc = pc;
	decoded->instruction = instruction;
	decoded->length = length;
	decoded->rd = (uint8)((instruction >> 7U) & 31U);
	decoded->rs1 = (uint8)((instruction >> 15U) & 31U);
	decoded->rs2 = (uint8)((instruction >> 20U) & 31U);
	decoded->aux = (uint8)((instruction >> 12U) & 7U);
	decoded->immediate = decode_immediate(instruction, entry->immediate_kind);
	decoded->execute = entry->execute;
	select_specialisation(decoded);
	decoded->valid = 1U;
	return TRUE;
}

void riscv_decode_cache_invalidate(
	TargetCoreType *core,
	uint64 address,
	uint32 size)
{
	uint64 first;
	uint64 last = address + size - 1U;
	uint64 position;
	RiscvDecodedInsn *entry;

	if (address >= 3U) {
		first = address - 3U;
	}
	else {
		first = 0U;
	}
	for (position = first & ~1ULL; position <= last; position += 2U) {
		entry = &core->decode_cache[
			(position >> 1U) & (RISCV_DECODE_CACHE_ENTRIES - 1U)];
		if (entry->valid && (entry->pc <= last) &&
			(entry->pc + entry->length > address)) {
			entry->valid = 0U;
		}
	}
}
