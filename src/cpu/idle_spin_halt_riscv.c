#include "athrill_idle_spin_halt.h"
#include "cpu.h"
#include "riscv_decode.h"

static std_bool riscv_branch_to_self(CoreIdType core_id, AthrillAddrType pc)
{
	TargetCoreType *core = &virtual_cpu.cores[core_id].core;
	RiscvDecodedInsn *decoded = riscv_decode_lookup(core, pc);

	/* Compressed C.J is expanded to the same JAL operation, with length 2. */
	return (decoded != NULL) && decoded->valid && (decoded->pc == pc)
		&& (decoded->execute == op_exec_jal) && (decoded->rd == 0U)
		&& (decoded->immediate == 0U);
}

static std_bool riscv_halt_ready(CoreIdType core_id)
{
	TargetCoreType *core = &virtual_cpu.cores[core_id].core;

	return (core->reg.csr.mip & core->reg.csr.mie) == 0U;
}

static const AthrillIdleSpinHaltTargetOps riscv_idle_spin_halt_ops = {
	.name = "RISC-V",
	.is_branch_to_self = riscv_branch_to_self,
	.halt_ready = riscv_halt_ready,
};

void riscv_idle_spin_halt_init(void)
{
	athrill_idle_spin_halt_register_target(&riscv_idle_spin_halt_ops);
}
