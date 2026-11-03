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

typedef struct {
	uint8 source;
	uint8 destination;
	uint8 width;
} RiscvCompressedSlice;

enum {
	RISCV_COMPRESSED_REGISTER_NONE,
	RISCV_COMPRESSED_REGISTER_FIXED,
	RISCV_COMPRESSED_REGISTER_FIELD,
	RISCV_COMPRESSED_REGISTER_PRIME
};

enum {
	RISCV_COMPRESSED_INVALID_IMMEDIATE = 1U << 0,
	RISCV_COMPRESSED_INVALID_RD_ZERO = 1U << 1,
	RISCV_COMPRESSED_INVALID_RD_X2 = 1U << 2,
	RISCV_COMPRESSED_INVALID_RS1_ZERO = 1U << 3,
	RISCV_COMPRESSED_INVALID_RS2_ZERO = 1U << 4
};

typedef struct {
	uint8 kind;
	uint8 value;
	uint8 width;
} RiscvCompressedRegister;

typedef struct {
	uint16 mask;
	uint16 match;
	uint32 base;
	RiscvCompressedRegister registers[3];
	uint8 immediate_kind;
	uint8 immediate_width;
	uint8 immediate_scale;
	uint8 invalid;
	uint8 slice_count;
	RiscvCompressedSlice slices[8];
} RiscvCompressedEntry;

extern const RiscvDispatchEntry riscv_instruction_table[];
extern const uint32 riscv_instruction_table_count;
extern const RiscvCompressedEntry riscv_compressed_table[];
extern const uint32 riscv_compressed_table_count;
extern const uint8 riscv_compressed_ranges[32][2];

void riscv_decode_init(void);
const RiscvDispatchEntry *riscv_decode(uint32 instruction);
std_bool riscv_decode_fill(
	uint64 pc,
	uint32 instruction,
	uint8 length,
	RiscvDecodedInsn *decoded);
std_bool riscv_expand_compressed(uint16 instruction, uint32 *expanded);
/* The decode page holding pc, or NULL when there is none yet. */
static inline RiscvDecodePage *riscv_decode_page(TargetCoreType *core, uint64 pc)
{
	RiscvDecodePage *page =
		core->decode_pages[(pc >> RISCV_DECODE_PAGE_SHIFT) & (RISCV_DECODE_DIRECTORY_SIZE - 1U)];

	if ((page == NULL) || (page->base != (pc & ~((1ULL << RISCV_DECODE_PAGE_SHIFT) - 1U)))) {
		return NULL;
	}
	return page;
}

/* The entry for pc in its page. */
static inline RiscvDecodedInsn *riscv_decode_page_entry(RiscvDecodePage *page, uint64 pc)
{
	return &page->entries[(pc & ((1ULL << RISCV_DECODE_PAGE_SHIFT) - 1U)) >> 1U];
}

/* The cached entry for pc, or NULL when its page has none yet. */
static inline RiscvDecodedInsn *riscv_decode_lookup(TargetCoreType *core, uint64 pc)
{
	RiscvDecodePage *page = riscv_decode_page(core, pc);

	if (page == NULL) {
		return NULL;
	}
	return riscv_decode_page_entry(page, pc);
}

/* The entry for pc, allocating (or replacing) its page; NULL if out of memory. */
RiscvDecodedInsn *riscv_decode_slot(TargetCoreType *core, uint64 pc);
/* Frees all pages (core reset). */
void riscv_decode_cache_clear(TargetCoreType *core);
void riscv_decode_cache_invalidate(
	TargetCoreType *core,
	uint64 address,
	uint32 size);

#endif
