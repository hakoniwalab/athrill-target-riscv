#include "op_exec.h"
#include "op_exec_memory.h"

#include "bus.h"
#include "cpu_config.h"
#include "pmp.h"
#include "riscv_decode.h"

static int store_value(
	TargetCoreType *core,
	const RiscvDecodedInsn *decoded,
	uint32 width)
{
	uint64 address = core->reg.x[decoded->rs1] + decoded->immediate;
	uint64 value = core->reg.x[decoded->rs2];
	Std_ReturnType error = STD_E_OK;
	uint32 index;
	uint8 *pointer;

	if (!riscv_pmp_check(core, address, width, CpuMemoryAccess_WRITE)) {
		return op_exec_memory_fault(core, address, 7U);
	}
	if (((address & (width - 1U)) != 0U) && !CPU_CONFIG_ERBIUM_COMPAT) {
		return op_exec_memory_fault(core, address, 6U);
	}
	pointer = op_exec_memory_pointer(core, address, width, TRUE);
	if (pointer != NULL) {
		op_exec_memory_write(pointer, width, value);
	}
	else if (((address & (width - 1U)) != 0U) && CPU_CONFIG_ERBIUM_COMPAT) {
		/* Erbium accepts unaligned accesses one byte at a time. */
		for (index = 0U; (index < width) && (error == STD_E_OK); index++) {
			error = bus_put_data8(
				core->core_id,
				address + index,
				(uint8)(value >> (8U * index)));
		}
	}
	else if ((address & (width - 1U)) != 0U) {
		return op_exec_memory_fault(core, address, 6U);
	}
	else if (width == 1U) {
		error = bus_put_data8(core->core_id, address, (uint8)value);
	}
	else if (width == 2U) {
		error = bus_put_data16(core->core_id, address, (uint16)value);
	}
	else {
		error = bus_put_data32(core->core_id, address, (uint32)value);
		if ((error == STD_E_OK) && (width == 8U)) {
			error = bus_put_data32(
				core->core_id, address + 4U, (uint32)(value >> 32U));
		}
	}
	if (error != STD_E_OK) {
		return op_exec_memory_fault(core, address, 7U);
	}
	riscv_trace_store(core, address, (uint8)width, value);
	riscv_decode_cache_invalidate(core, address, width);
	return op_exec_advance(core, decoded);
}

#define STORE(name, width) \
	int op_exec_##name( \
		TargetCoreType *core, \
		const RiscvDecodedInsn *decoded) \
	{ \
		return store_value(core, decoded, width); \
	}

STORE(sb, 1U)
STORE(sh, 2U)
STORE(sw, 4U)
STORE(sd, 8U)
