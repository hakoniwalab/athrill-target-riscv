#include "op_exec.h"

#include "bus.h"
#include "cpu_config.h"
#include "pmp.h"

int op_exec_memory_fault(TargetCoreType *core, uint64 address, uint64 cause)
{
	riscv_raise_exception(core, cause, address);
	core->access_fault = TRUE;
	return RISCV_EXEC_TRAP;
}

static int load_value(
	TargetCoreType *core,
	const RiscvDecodedInsn *decoded,
	uint32 width,
	std_bool sign_extend)
{
	uint64 address = core->reg.x[decoded->rs1] + decoded->immediate;
	uint64 value = 0U;
	uint32 low;
	uint32 high;
	uint32 index;
	uint16 half;
	uint8 byte;

	if (!riscv_pmp_check(core, address, width, CpuMemoryAccess_READ)) {
		return op_exec_memory_fault(core, address, 5U);
	}
	if (((address & (width - 1U)) != 0U) && CPU_CONFIG_ERBIUM_COMPAT) {
		/* Erbium accepts unaligned accesses one byte at a time. */
		for (index = 0U; index < width; index++) {
			if (bus_get_data8(core->core_id, address + index, &byte) !=
				STD_E_OK) {
				return op_exec_memory_fault(core, address, 5U);
			}
			value |= (uint64)byte << (8U * index);
		}
	}
	else if ((address & (width - 1U)) != 0U) {
		return op_exec_memory_fault(core, address, 4U);
	}
	else if (width == 1U) {
		if (bus_get_data8(core->core_id, address, &byte) != STD_E_OK) {
			return op_exec_memory_fault(core, address, 5U);
		}
		value = byte;
	}
	else if (width == 2U) {
		if (bus_get_data16(core->core_id, address, &half) != STD_E_OK) {
			return op_exec_memory_fault(core, address, 5U);
		}
		value = half;
	}
	else {
		if (bus_get_data32(core->core_id, address, &low) != STD_E_OK) {
			return op_exec_memory_fault(core, address, 5U);
		}
		value = low;
		if (width == 8U) {
			if (bus_get_data32(core->core_id, address + 4U, &high) !=
				STD_E_OK) {
				return op_exec_memory_fault(core, address, 5U);
			}
			value |= (uint64)high << 32U;
		}
	}
	if (sign_extend) {
		value = op_exec_sign_extend(value, width * 8U);
	}
	if (decoded->rd != 0U) {
		core->reg.x[decoded->rd] = value;
	}
	return op_exec_advance(core, decoded);
}

#define LOAD(name, width, sign_extend) \
	int op_exec_##name( \
		TargetCoreType *core, \
		const RiscvDecodedInsn *decoded) \
	{ \
		return load_value(core, decoded, width, sign_extend); \
	}

/* Signed loads extend their memory operand to XLEN. */
LOAD(lb, 1U, TRUE)
LOAD(lh, 2U, TRUE)
LOAD(lw, 4U, TRUE)
LOAD(ld, 8U, TRUE)
LOAD(lbu, 1U, FALSE)
LOAD(lhu, 2U, FALSE)
LOAD(lwu, 4U, FALSE)
