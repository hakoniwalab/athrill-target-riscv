#include "op_exec.h"

#include "cpu_config.h"
#include "pmp.h"

int op_exec_fence(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return op_exec_advance(core, decoded);
}

int op_exec_fence_i(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	/* Erbium reports its implementation-specific fence.i exception. */
	if (CPU_CONFIG_ERBIUM_COMPAT) {
		riscv_raise_exception(core, 30U, decoded->instruction);
		return RISCV_EXEC_TRAP;
	}
	return op_exec_advance(core, decoded);
}

int op_exec_ecall(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	(void)decoded;
	riscv_raise_exception(core, 11U, 0U);
	return RISCV_EXEC_TRAP;
}

int op_exec_ebreak(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	(void)decoded;
	riscv_raise_exception(core, 3U, 0U);
	return RISCV_EXEC_TRAP;
}

int op_exec_mret(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint64 status = core->reg.csr.mstatus;
	uint64 target = core->reg.csr.mepc;

	(void)decoded;
	if ((target & 1U) != 0U) {
		riscv_raise_exception(core, 0U, target);
		return RISCV_EXEC_TRAP;
	}
	core->reg.privilege = (RiscvPrivilege)((status >> 11U) & 3U);
	status &= ~(1ULL << 3U);
	status |= ((status >> 7U) & 1U) << 3U;
	status |= 1ULL << 7U;
	status &= ~(3ULL << 11U);
	core->reg.csr.mstatus = status;
	core->reg.pc = target;
	return RISCV_EXEC_RETIRED;
}

int op_exec_wfi(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	if ((core->reg.csr.mip & core->reg.csr.mie) == 0U) {
		core->is_halt = TRUE;
	}
	return op_exec_advance(core, decoded);
}

static uint64 *csr_pointer(TargetCoreType *core, uint32 number)
{
	if ((number >= 0x3b0U) && (number <= 0x3bfU)) {
		return &core->reg.csr.pmpaddr[number - 0x3b0U];
	}
	switch (number) {
	case 0x100U:
		return &core->reg.csr.sstatus;
	case 0x104U:
		return &core->reg.csr.sie;
	case 0x105U:
		return &core->reg.csr.stvec;
	case 0x106U:
		return &core->reg.csr.scounteren;
	case 0x140U:
		return &core->reg.csr.sscratch;
	case 0x141U:
		return &core->reg.csr.sepc;
	case 0x142U:
		return &core->reg.csr.scause;
	case 0x143U:
		return &core->reg.csr.stval;
	case 0x144U:
		return &core->reg.csr.sip;
	case 0x180U:
		return &core->reg.csr.satp;
	case 0x300U:
		return &core->reg.csr.mstatus;
	case 0x301U:
		return &core->reg.csr.misa;
	case 0x302U:
		return &core->reg.csr.medeleg;
	case 0x303U:
		return &core->reg.csr.mideleg;
	case 0x304U:
		return &core->reg.csr.mie;
	case 0x305U:
		return &core->reg.csr.mtvec;
	case 0x306U:
		return &core->reg.csr.mcounteren;
	case 0x30aU:
		return &core->reg.csr.menvcfg;
	case 0x340U:
		return &core->reg.csr.mscratch;
	case 0x341U:
		return &core->reg.csr.mepc;
	case 0x342U:
		return &core->reg.csr.mcause;
	case 0x343U:
		return &core->reg.csr.mtval;
	case 0x344U:
		return &core->reg.csr.mip;
	case 0x3a0U:
		return &core->reg.csr.pmpcfg[0];
	case 0x3a2U:
		return &core->reg.csr.pmpcfg[1];
	case 0xf14U:
		return &core->reg.csr.mhartid;
	default:
		return 0;
	}
}

static std_bool fp_csr_access(
	TargetCoreType *core,
	uint32 number,
	uint64 *value,
	std_bool write)
{
	uint32 mask;

	if ((number < 1U) || (number > 3U)) {
		return FALSE;
	}
	if (((core->reg.csr.mstatus >> 13U) & 3U) == 0U) {
		return FALSE;
	}
	mask = (number == 1U) ? 0x8000001fU : ((number == 2U) ? 7U : 0x800000ffU);
	if (write) {
		if (number == 1U) {
			core->reg.fcsr = (core->reg.fcsr & ~mask) | ((uint32)*value & mask);
		}
		else if (number == 2U) {
			core->reg.fcsr = (core->reg.fcsr & ~0xe0U) | (((uint32)*value & 7U) << 5U);
		}
		else {
			core->reg.fcsr = (uint32)*value & mask;
		}
		core->reg.csr.mstatus |= 3ULL << 13U;
	}
	else if (number == 1U) {
		*value = core->reg.fcsr & mask;
	}
	else if (number == 2U) {
		*value = (core->reg.fcsr >> 5U) & 7U;
	}
	else {
		*value = core->reg.fcsr & mask;
	}
	return TRUE;
}

static int execute_csr(
	TargetCoreType *core,
	const RiscvDecodedInsn *decoded,
	uint32 operation,
	std_bool immediate)
{
	uint32 number = decoded->instruction >> 20U;
	uint64 *pointer = csr_pointer(core, number);
	uint64 source;
	uint64 old;
	uint64 value;
	std_bool user_counter;
	std_bool counter;
	std_bool write;

	if (immediate) {
		source = decoded->rs1;
	}
	else {
		source = core->reg.x[decoded->rs1];
	}
	user_counter = (number == 0xc00U) || (number == 0xc02U);
	counter = (number == 0xb00U) || (number == 0xb02U) || user_counter;
	write = (operation == 1U) || (source != 0U);
	if ((number >= 1U) && (number <= 3U)) {
		if (!fp_csr_access(core, number, &old, FALSE)) {
			riscv_raise_exception(core, 2U, decoded->instruction);
			return RISCV_EXEC_TRAP;
		}
		if (write) {
			if (operation == 1U) {
				value = source;
			}
			else if (operation == 2U) {
				value = old | source;
			}
			else {
				value = old & ~source;
			}
			(void)fp_csr_access(core, number, &value, TRUE);
		}
		if (decoded->rd != 0U) {
			core->reg.x[decoded->rd] = old;
		}
		return op_exec_advance(core, decoded);
	}
	if (counter) {
		if (write && user_counter) {
			riscv_raise_exception(core, 2U, decoded->instruction);
			return RISCV_EXEC_TRAP;
		}
		/* Erbium exposes the legacy zero-valued counter behaviour. */
		if (CPU_CONFIG_ERBIUM_COMPAT) {
			old = 0U;
		}
		else if ((number == 0xb00U) || (number == 0xc00U)) {
			old = core->internal_cycle;
		}
		else {
			old = core->internal_instret;
		}
		if (write && !CPU_CONFIG_ERBIUM_COMPAT) {
			if (operation == 1U) {
				value = source;
			}
			else if (operation == 2U) {
				value = old | source;
			}
			else {
				value = old & ~source;
			}
			if (number == 0xb00U) {
				core->internal_cycle = value;
			}
			else {
				core->internal_instret = value;
			}
		}
	}
	else {
		if ((pointer == 0) ||
			(core->reg.privilege < RISCV_PRIV_M) ||
			(write && (((number >> 10U) & 3U) == 3U))) {
			riscv_raise_exception(core, 2U, decoded->instruction);
			return RISCV_EXEC_TRAP;
		}
		old = *pointer;
		if (write) {
			if (operation == 1U) {
				*pointer = source;
			}
			else if (operation == 2U) {
				*pointer = old | source;
			}
			else {
				*pointer = old & ~source;
			}
			if ((number == 0x3a0U) || (number == 0x3a2U) ||
				((number >= 0x3b0U) && (number <= 0x3bfU))) {
				core->pmp_epoch++;
			}
		}
	}
	if (decoded->rd != 0U) {
		core->reg.x[decoded->rd] = old;
	}
	return op_exec_advance(core, decoded);
}

#define CSR_EXEC(name, operation, immediate) \
	int op_exec_##name( \
		TargetCoreType *core, \
		const RiscvDecodedInsn *decoded) \
	{ \
		return execute_csr(core, decoded, operation, immediate); \
	}

CSR_EXEC(csrrw, 1U, FALSE)
CSR_EXEC(csrrs, 2U, FALSE)
CSR_EXEC(csrrc, 3U, FALSE)
CSR_EXEC(csrrwi, 1U, TRUE)
CSR_EXEC(csrrsi, 2U, TRUE)
CSR_EXEC(csrrci, 3U, TRUE)
