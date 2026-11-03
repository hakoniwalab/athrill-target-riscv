#ifndef ATHRILL_RISCV_OP_EXEC_MEMORY_H
#define ATHRILL_RISCV_OP_EXEC_MEMORY_H

#include "std_errno.h"
#include "target_cpu.h"
#include "mpu_ops.h"
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
		if ((address < window->start) || (address > window->end - width)) {
			return NULL;
		}
	}
	if (write && !window->writable) {
		return NULL;
	}
	return window->data + (address - window->start);
}

/* Little-endian load of width bytes (Athrill hosts are little endian). */
static inline uint64 op_exec_memory_read(const uint8 *pointer, uint32 width)
{
	uint64 value = 0U;

	memcpy(&value, pointer, width);
	return value;
}

static inline void op_exec_memory_write(uint8 *pointer, uint32 width, uint64 value)
{
	memcpy(pointer, &value, width);
}

#endif
