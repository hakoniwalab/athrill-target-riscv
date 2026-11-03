#ifndef ATHRILL_RISCV_OP_EXEC_MEMORY_H
#define ATHRILL_RISCV_OP_EXEC_MEMORY_H

#include "std_errno.h"
#include "target_cpu.h"
#include "mpu_ops.h"
#include "pmp.h"
#include <string.h>

/*
 * Direct access to ROM and RAM. Each core keeps the memory region it
 * accessed last (its range and host buffer, see mpu_get_memory_window()).
 * A load or store that lies wholly inside it reads or writes the buffer;
 * anything else (devices, other regions) goes through the bus as before.
 * Regions do not move after loading, so the window stays valid; a core
 * reset clears it. PMP and alignment are checked by the caller first.
 *
 * Returns the host address of the access, or NULL to use the bus.
 */
static inline uint8 *op_exec_memory_pointer(TargetCoreType *core, uint64 address, uint32 width, std_bool write)
{
	RiscvMemoryWindow *window = &core->memory_window;
	AthrillAddrType start;
	AthrillAddrType size;
	uint8 *data;
	std_bool writable;

	if (!window->valid || (address < window->start) || (address > window->end - width)) {
		if (mpu_get_memory_window(core->core_id, (AthrillAddrType)address, &start, &size, &data,
			&writable) != STD_E_OK) {
			return NULL;
		}
		window->start = start;
		window->end = start + size;
		window->data = data;
		window->writable = writable;
		window->valid = TRUE;
		window->pmp_valid = FALSE;
		if ((address < window->start) || (address > window->end - width)) {
			return NULL;
		}
	}
	if (write && !window->writable) {
		return NULL;
	}
	return window->data + (address - window->start);
}

/*
 * The host address of an access wholly inside the window when PMP allows
 * the access type on the whole window; NULL otherwise, and the caller
 * checks PMP and uses op_exec_memory_pointer(). Allowing the whole window
 * means that the first PMP entry overlapping it contains it (or none
 * overlaps it, in Machine mode), so that entry is also the first one for
 * any access inside it and allows that access too.
 */
static inline uint8 *op_exec_memory_direct(TargetCoreType *core, uint64 address, uint32 width,
	CpuMemoryAccessType access)
{
	RiscvMemoryWindow *window = &core->memory_window;

	if (!window->valid || (address < window->start) || (address > window->end - width)) {
		return NULL;
	}
	if (!window->pmp_valid || (window->pmp_epoch != core->pmp_epoch) ||
		(window->pmp_privilege != core->reg.privilege)) {
		/* riscv_pmp_check() takes a 32-bit size; larger windows are checked per access. */
		std_bool fits = (window->end - window->start) <= 0xffffffffULL;
		uint32 size = (uint32)(window->end - window->start);

		window->pmp_read = fits && riscv_pmp_check(core, window->start, size, CpuMemoryAccess_READ);
		window->pmp_write = fits && window->writable &&
			riscv_pmp_check(core, window->start, size, CpuMemoryAccess_WRITE);
		window->pmp_epoch = core->pmp_epoch;
		window->pmp_privilege = core->reg.privilege;
		window->pmp_valid = TRUE;
	}
	if ((access == CpuMemoryAccess_WRITE) ? !window->pmp_write : !window->pmp_read) {
		return NULL;
	}
	return window->data + (address - window->start);
}

/*
 * Little-endian load and store of width bytes (Athrill hosts are little
 * endian). Each width is its own fixed-size copy so that the compiler
 * makes it one host load or store instead of a call to memmove.
 */
static inline uint64 op_exec_memory_read(const uint8 *pointer, uint32 width)
{
	uint8 value8;
	uint16 value16;
	uint32 value32;
	uint64 value64;

	switch (width) {
	case 1U:
		memcpy(&value8, pointer, 1U);
		return value8;
	case 2U:
		memcpy(&value16, pointer, 2U);
		return value16;
	case 4U:
		memcpy(&value32, pointer, 4U);
		return value32;
	default:
		memcpy(&value64, pointer, 8U);
		return value64;
	}
}

static inline void op_exec_memory_write(uint8 *pointer, uint32 width, uint64 value)
{
	uint8 value8 = (uint8)value;
	uint16 value16 = (uint16)value;
	uint32 value32 = (uint32)value;

	switch (width) {
	case 1U:
		memcpy(pointer, &value8, 1U);
		break;
	case 2U:
		memcpy(pointer, &value16, 2U);
		break;
	case 4U:
		memcpy(pointer, &value32, 4U);
		break;
	default:
		memcpy(pointer, &value, 8U);
		break;
	}
}

#endif
