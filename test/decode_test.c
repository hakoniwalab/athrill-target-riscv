#include <stdio.h>
#include <string.h>

#include "riscv_decode.h"

typedef struct {
	uint32 instruction;
	const char *mnemonic;
	uint8 rd;
	uint8 rs1;
	uint8 rs2;
	uint64 immediate;
} DecodeExpectation;

static uint64 reference_sign_extend(uint64 value, uint32 width)
{
	uint32 shift = 64U - width;

	return (uint64)((sint64)(value << shift) >> shift);
}

static uint64 reference_immediate(uint32 instruction)
{
	uint32 opcode = instruction & 127U;
	uint64 value;

	if ((opcode == 0x03U) || (opcode == 0x13U) ||
		(opcode == 0x1bU) || (opcode == 0x67U)) {
		return reference_sign_extend(instruction >> 20U, 12U);
	}
	if (opcode == 0x23U) {
		value = ((instruction >> 25U) << 5U);
		value |= (instruction >> 7U) & 31U;
		return reference_sign_extend(value, 12U);
	}
	if (opcode == 0x63U) {
		value = ((uint64)instruction >> 31U) << 12U;
		value |= ((instruction >> 7U) & 1U) << 11U;
		value |= ((instruction >> 25U) & 63U) << 5U;
		value |= ((instruction >> 8U) & 15U) << 1U;
		return reference_sign_extend(value, 13U);
	}
	if ((opcode == 0x17U) || (opcode == 0x37U)) {
		return reference_sign_extend(instruction & 0xfffff000U, 32U);
	}
	if (opcode == 0x6fU) {
		value = ((uint64)instruction >> 31U) << 20U;
		value |= instruction & 0x000ff000U;
		value |= ((instruction >> 20U) & 1U) << 11U;
		value |= ((instruction >> 21U) & 1023U) << 1U;
		return reference_sign_extend(value, 21U);
	}
	return 0U;
}

static int check_instruction(const DecodeExpectation *expected)
{
	RiscvDecodedInsn decoded;
	const RiscvDispatchEntry *entry;

	entry = riscv_decode(expected->instruction);
	if (entry == 0) {
		printf("0x%08x: illegal\n", expected->instruction);
		return 1;
	}
	if (strcmp(entry->mnemonic, expected->mnemonic) != 0) {
		printf("0x%08x: expected %s, got %s\n",
			expected->instruction, expected->mnemonic, entry->mnemonic);
		return 1;
	}
	if (!riscv_decode_fill(0x40000238U, expected->instruction, 4U,
		&decoded)) {
		return 1;
	}
	if ((decoded.rd != expected->rd) || (decoded.rs1 != expected->rs1) ||
		(decoded.rs2 != expected->rs2) ||
		(decoded.immediate != expected->immediate) ||
		(decoded.immediate != reference_immediate(expected->instruction))) {
		printf("0x%08x: fields %u/%u/%u/0x%llx\n",
			expected->instruction, decoded.rd, decoded.rs1, decoded.rs2,
			(unsigned long long)decoded.immediate);
		return 1;
	}
	if (decoded.execute != entry->execute) {
		printf("0x%08x: wrong handler\n", expected->instruction);
		return 1;
	}
	return 0;
}

int main(void)
{
	static const DecodeExpectation expectations[] = {
		{0x00072783U, "lw", 15U, 14U, 0U, 0U},
		{0x0027f793U, "andi", 15U, 15U, 2U, 2U},
		{0xfe079ce3U, "bne", 25U, 15U, 0U, ~7ULL},
		{0xfef72c23U, "sw", 24U, 14U, 15U, ~7ULL},
		{0xfffff297U, "auipc", 5U, 31U, 31U, ~4095ULL},
		{0xff9ff06fU, "jal", 0U, 31U, 25U, ~7ULL}
	};
	uint32 index;

	for (index = 0U; index <
		(uint32)(sizeof(expectations) / sizeof(expectations[0])); index++) {
		if (check_instruction(&expectations[index]) != 0) {
			return 1;
		}
	}
	return 0;
}
