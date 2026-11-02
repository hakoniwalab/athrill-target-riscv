#include "op_exec.h"
#include "pmp.h"

static int jump_target(
	TargetCoreType *core,
	const RiscvDecodedInsn *decoded,
	uint64 target)
{
	if ((target & 1U) != 0U) {
		riscv_raise_exception(core, 0U, target);
		return RISCV_EXEC_TRAP;
	}
	if (decoded->rd != 0U) {
		core->reg.x[decoded->rd] = core->reg.pc + decoded->length;
	}
	core->reg.pc = target;
	return RISCV_EXEC_RETIRED;
}

int op_exec_jal(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return jump_target(core, decoded, core->reg.pc + decoded->immediate);
}

int op_exec_jalr(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint64 target;

	target = core->reg.x[decoded->rs1] + decoded->immediate;
	target &= ~1ULL;
	return jump_target(core, decoded, target);
}

static int branch_if(
	TargetCoreType *core,
	const RiscvDecodedInsn *decoded,
	std_bool taken)
{
	uint64 target;

	if (!taken) {
		return op_exec_advance(core, decoded);
	}
	target = core->reg.pc + decoded->immediate;
	if ((target & 1U) != 0U) {
		riscv_raise_exception(core, 0U, target);
		return RISCV_EXEC_TRAP;
	}
	core->reg.pc = target;
	return RISCV_EXEC_RETIRED;
}

int op_exec_beq(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return branch_if(
		core,
		decoded,
		core->reg.x[decoded->rs1] == core->reg.x[decoded->rs2]);
}

int op_exec_bne(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return branch_if(
		core,
		decoded,
		core->reg.x[decoded->rs1] != core->reg.x[decoded->rs2]);
}

int op_exec_blt(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return branch_if(
		core,
		decoded,
		(sint64)core->reg.x[decoded->rs1] <
		(sint64)core->reg.x[decoded->rs2]);
}

int op_exec_bge(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return branch_if(
		core,
		decoded,
		(sint64)core->reg.x[decoded->rs1] >=
		(sint64)core->reg.x[decoded->rs2]);
}

int op_exec_bltu(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return branch_if(
		core,
		decoded,
		core->reg.x[decoded->rs1] < core->reg.x[decoded->rs2]);
}

int op_exec_bgeu(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return branch_if(
		core,
		decoded,
		core->reg.x[decoded->rs1] >= core->reg.x[decoded->rs2]);
}
