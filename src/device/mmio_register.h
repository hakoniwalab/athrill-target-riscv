#ifndef ATHRILL_MMIO_REGISTER_H
#define ATHRILL_MMIO_REGISTER_H

#include "std_types.h"

/*
 * Byte, halfword and word accesses to a block of 32-bit device registers.
 *
 * Registers can have side effects on access (a PLIC claim, a UART receive
 * pop, a UART transmit), so every access touches each 32-bit register it
 * covers exactly once:
 *   - a read reads each covered register once and returns the addressed
 *     bytes (little endian);
 *   - a write writes each covered register once with the addressed bytes
 *     in their lanes and zero in the other bytes; it does not read the
 *     register first.
 * An access that crosses a word boundary (possible with misaligned
 * accesses) covers two registers.
 */
typedef uint32 (*MmioReadRegister)(AthrillAddrType word_address);
typedef void (*MmioWriteRegister)(AthrillAddrType word_address, uint32 value);

static inline uint32 mmio_read(MmioReadRegister read, AthrillAddrType address, uint32 size)
{
	AthrillAddrType word = address & ~(AthrillAddrType)3U;
	uint32 shift = (uint32)(address & 3U) * 8U;
	uint64 bytes = (uint64)read(word) >> shift;
	uint32 first_word_bytes = 4U - (uint32)(address & 3U);
	uint64 mask = (size >= 4U) ? 0xffffffffULL : ((1ULL << (size * 8U)) - 1U);

	if (size > first_word_bytes) {
		bytes |= (uint64)read(word + 4U) << (first_word_bytes * 8U);
	}
	return (uint32)(bytes & mask);
}

static inline void mmio_write(MmioWriteRegister write, AthrillAddrType address, uint32 size, uint32 data)
{
	AthrillAddrType word = address & ~(AthrillAddrType)3U;
	uint32 shift = (uint32)(address & 3U) * 8U;
	uint32 first_word_bytes = 4U - (uint32)(address & 3U);
	uint64 mask = (size >= 4U) ? 0xffffffffULL : ((1ULL << (size * 8U)) - 1U);
	uint64 value = ((uint64)data & mask) << shift;

	write(word, (uint32)value);
	if (size > first_word_bytes) {
		write(word + 4U, (uint32)(value >> 32U));
	}
}

#endif /* ATHRILL_MMIO_REGISTER_H */
