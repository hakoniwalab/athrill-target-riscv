#include "riscv_decode.h"
#include <stdlib.h>

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

static uint32 compressed_register(
	uint16 instruction,
	const RiscvCompressedRegister *recipe)
{
	uint32 mask;
	uint32 value;

	if (recipe->kind == RISCV_COMPRESSED_REGISTER_NONE) {
		return 0U;
	}
	if (recipe->kind == RISCV_COMPRESSED_REGISTER_FIXED) {
		return recipe->value;
	}
	mask = (1U << recipe->width) - 1U;
	value = ((uint32)instruction >> recipe->value) & mask;
	if (recipe->kind == RISCV_COMPRESSED_REGISTER_PRIME) {
		value += 8U;
	}
	return value;
}

static uint32 encode_compressed_immediate(uint32 value, uint8 kind)
{
	switch (kind) {
	case RISCV_IMMEDIATE_I:
		return (value & 0xfffU) << 20U;
	case RISCV_IMMEDIATE_S:
		return ((value & 0xfe0U) << 20U) |
			((value & 0x1fU) << 7U);
	case RISCV_IMMEDIATE_B:
		return ((value & 0x1000U) << 19U) |
			((value & 0x7e0U) << 20U) |
			((value & 0x1eU) << 7U) |
			((value & 0x800U) >> 4U);
	case RISCV_IMMEDIATE_U:
		return value & 0xfffff000U;
	case RISCV_IMMEDIATE_J:
		return ((value & 0x100000U) << 11U) |
			(value & 0xff000U) |
			((value & 0x800U) << 9U) |
			((value & 0x7feU) << 20U);
	default:
		return 0U;
	}
}

std_bool riscv_expand_compressed(uint16 instruction, uint32 *expanded)
{
	const RiscvCompressedEntry *entry;
	uint32 immediate;
	uint32 index;
	uint32 limit;
	uint32 mask;
	uint32 rd;
	uint32 rs1;
	uint32 rs2;
	uint32 slice;

	index = (((uint32)instruction >> 13U) << 2U) | (instruction & 3U);
	limit = riscv_compressed_ranges[index][0] +
		riscv_compressed_ranges[index][1];
	index = riscv_compressed_ranges[index][0];
	for (; index < limit; index++) {
		entry = &riscv_compressed_table[index];
		if ((instruction & entry->mask) != entry->match) {
			continue;
		}
		immediate = 0U;
		for (slice = 0U; slice < entry->slice_count; slice++) {
			mask = (1U << entry->slices[slice].width) - 1U;
			immediate |= (((uint32)instruction >>
				entry->slices[slice].source) & mask) <<
				entry->slices[slice].destination;
		}
		if (((entry->invalid & RISCV_COMPRESSED_INVALID_IMMEDIATE) != 0U) &&
			(immediate == 0U)) {
			return FALSE;
		}
		if ((entry->immediate_width != 0U) &&
			((immediate & (1U << (entry->immediate_width - 1U))) != 0U)) {
			immediate |= ~((1U << entry->immediate_width) - 1U);
		}
		rd = compressed_register(instruction, &entry->registers[0]);
		rs1 = compressed_register(instruction, &entry->registers[1]);
		rs2 = compressed_register(instruction, &entry->registers[2]);
		if (((entry->invalid & RISCV_COMPRESSED_INVALID_RD_ZERO) != 0U) &&
			(rd == 0U)) {
			return FALSE;
		}
		if (((entry->invalid & RISCV_COMPRESSED_INVALID_RD_X2) != 0U) &&
			(rd == 2U)) {
			return FALSE;
		}
		if (((entry->invalid & RISCV_COMPRESSED_INVALID_RS1_ZERO) != 0U) &&
			(rs1 == 0U)) {
			return FALSE;
		}
		if (((entry->invalid & RISCV_COMPRESSED_INVALID_RS2_ZERO) != 0U) &&
			(rs2 == 0U)) {
			return FALSE;
		}
		*expanded = entry->base | (rd << 7U) | (rs1 << 15U) |
			(rs2 << 20U) |
			encode_compressed_immediate(immediate, entry->immediate_kind);
		return TRUE;
	}
	return FALSE;
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
	uint32 scan;
	uint32 opcode = instruction & 127U;
	const RiscvDispatchEntry *entry;

	if (!decode_initialized) {
		riscv_decode_init();
	}
	/* OP-FP and the four fused opcodes contain several encodings whose rm
	 * field is variable.  They cannot share the integer decoder's funct3
	 * direct slot, so select them by their generated masks. */
	if ((opcode == 0x53U) || (opcode == 0x43U) || (opcode == 0x47U) ||
		(opcode == 0x4bU) || (opcode == 0x4fU)) {
		for (scan = 0U; scan < riscv_instruction_table_count; scan++) {
			entry = &riscv_instruction_table[scan];
			if ((instruction & entry->mask) == entry->match) {
				return entry;
			}
		}
		return 0;
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
	decoded->rs3 = (uint8)((instruction >> 27U) & 31U);
	decoded->aux = (uint8)((instruction >> 12U) & 7U);
	decoded->immediate = decode_immediate(instruction, entry->immediate_kind);
	decoded->execute = entry->execute;
	select_specialisation(decoded);
	decoded->block_length = 0U;
	decoded->valid = 1U;
	return TRUE;
}

RiscvDecodedInsn *riscv_decode_slot(TargetCoreType *core, uint64 pc)
{
	uint64 base = pc & ~((1ULL << RISCV_DECODE_PAGE_SHIFT) - 1U);
	RiscvDecodePage **slot =
		&core->decode_pages[(pc >> RISCV_DECODE_PAGE_SHIFT) & (RISCV_DECODE_DIRECTORY_SIZE - 1U)];

	if ((*slot == NULL) || ((*slot)->base != base)) {
		free(*slot);
		*slot = calloc(1U, sizeof(RiscvDecodePage));
		if (*slot == NULL) {
			return NULL;
		}
		(*slot)->base = base;
		if ((core->decode_hi == 0U) || (base < core->decode_lo)) {
			core->decode_lo = base;
		}
		if (base + (1ULL << RISCV_DECODE_PAGE_SHIFT) > core->decode_hi) {
			core->decode_hi = base + (1ULL << RISCV_DECODE_PAGE_SHIFT);
		}
	}
	return &(*slot)->entries[(pc & ((1ULL << RISCV_DECODE_PAGE_SHIFT) - 1U)) >> 1U];
}

void riscv_decode_cache_clear(TargetCoreType *core)
{
	uint32 index;

	for (index = 0U; index < RISCV_DECODE_DIRECTORY_SIZE; index++) {
		free(core->decode_pages[index]);
		core->decode_pages[index] = NULL;
	}
	core->decode_lo = 0U;
	core->decode_hi = 0U;
}

void riscv_decode_cache_invalidate_pages(
	TargetCoreType *core,
	uint64 address,
	uint32 size)
{
	uint64 first;
	uint64 last = address + size - 1U;
	uint64 position;
	RiscvDecodePage *page;
	RiscvDecodedInsn *entry;

	/* A 4-byte instruction may start up to 3 bytes before the store. */
	if (address >= 3U) {
		first = address - 3U;
	}
	else {
		first = 0U;
	}
	for (position = first & ~1ULL; position <= last; position += 2U) {
		page = riscv_decode_page(core, position);
		if (page == NULL) {
			continue;
		}
		entry = riscv_decode_page_entry(page, position);
		if (entry->valid && (entry->pc <= last) && (entry->pc + entry->length > address)) {
			entry->valid = 0U;
			page->generation++;
		}
	}
}
