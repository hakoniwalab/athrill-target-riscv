#include "op_exec.h"

static int write_result(
	TargetCoreType *core,
	const RiscvDecodedInsn *decoded,
	uint64 result)
{
	/* Writes to x0 have no architectural effect. */
	if (decoded->rd != 0U) {
		core->reg.x[decoded->rd] = result;
	}
	return op_exec_advance(core, decoded);
}

int op_exec_lui(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return write_result(core, decoded, decoded->immediate);
}

int op_exec_auipc(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint64 result = core->reg.pc + decoded->immediate;

	return write_result(core, decoded, result);
}

int op_exec_addi(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint64 result = core->reg.x[decoded->rs1] + decoded->immediate;

	return write_result(core, decoded, result);
}

int op_exec_slti(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	sint64 source = (sint64)core->reg.x[decoded->rs1];
	sint64 immediate = (sint64)decoded->immediate;

	return write_result(core, decoded, source < immediate);
}

int op_exec_sltiu(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint64 source = core->reg.x[decoded->rs1];

	return write_result(core, decoded, source < decoded->immediate);
}

int op_exec_add(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint64 result;

	result = core->reg.x[decoded->rs1] + core->reg.x[decoded->rs2];
	return write_result(core, decoded, result);
}

int op_exec_sub(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint64 result;

	result = core->reg.x[decoded->rs1] - core->reg.x[decoded->rs2];
	return write_result(core, decoded, result);
}

int op_exec_slt(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	sint64 left = (sint64)core->reg.x[decoded->rs1];
	sint64 right = (sint64)core->reg.x[decoded->rs2];

	return write_result(core, decoded, left < right);
}

int op_exec_sltu(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint64 left = core->reg.x[decoded->rs1];
	uint64 right = core->reg.x[decoded->rs2];

	return write_result(core, decoded, left < right);
}

int op_exec_addiw(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint32 result;

	result = (uint32)core->reg.x[decoded->rs1];
	result += (uint32)decoded->immediate;
	/* RV64 W operations always sign-extend bit 31. */
	return write_result(core, decoded, op_exec_sign_extend_word(result));
}

int op_exec_addw(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint32 result;

	result = (uint32)core->reg.x[decoded->rs1];
	result += (uint32)core->reg.x[decoded->rs2];
	return write_result(core, decoded, op_exec_sign_extend_word(result));
}

int op_exec_subw(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint32 result;

	result = (uint32)core->reg.x[decoded->rs1];
	result -= (uint32)core->reg.x[decoded->rs2];
	return write_result(core, decoded, op_exec_sign_extend_word(result));
}
