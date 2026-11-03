/*
 * Print Athrill's decoding of instruction words, for comparison with an
 * independent disassembler (test/crosscheck_decoder.py).
 *
 * Input lines:  "w <hex>" for a 32-bit word, "c <hex>" for a 16-bit one.
 * Output lines: "w <hex> <mnemonic>|ILLEGAL"
 *               "c <hex> <expanded hex> <mnemonic>|ILLEGAL"
 */
#include <stdio.h>

#include "riscv_decode.h"

static void print_word(uint32 instruction)
{
	const RiscvDispatchEntry *entry = riscv_decode(instruction);

	printf("w %08x %s\n", instruction, entry != NULL ? entry->mnemonic : "ILLEGAL");
}

static void print_compressed(uint16 instruction)
{
	uint32 expanded;
	const RiscvDispatchEntry *entry;

	if (!riscv_expand_compressed(instruction, &expanded)) {
		printf("c %04x ILLEGAL\n", instruction);
		return;
	}
	entry = riscv_decode(expanded);
	printf("c %04x %08x %s\n", instruction, expanded, entry != NULL ? entry->mnemonic : "ILLEGAL");
}

int main(void)
{
	char kind;
	unsigned int value;

	riscv_decode_init();
	while (scanf(" %c %x", &kind, &value) == 2) {
		if (kind == 'w') {
			print_word((uint32)value);
		}
		else {
			print_compressed((uint16)value);
		}
	}
	return 0;
}
