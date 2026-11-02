#ifndef ATHRILL_RISCV_OP_EXEC_H
#define ATHRILL_RISCV_OP_EXEC_H

#include "target_cpu.h"

#define RISCV_EXEC_TRAP 0
#define RISCV_EXEC_RETIRED 1

#define OP_EXEC(name) \
	int op_exec_##name(TargetCoreType *core, const RiscvDecodedInsn *decoded)

OP_EXEC(lui);
OP_EXEC(auipc);
OP_EXEC(addi);
OP_EXEC(slti);
OP_EXEC(sltiu);
OP_EXEC(add);
OP_EXEC(sub);
OP_EXEC(slt);
OP_EXEC(sltu);
OP_EXEC(addiw);
OP_EXEC(addw);
OP_EXEC(subw);
OP_EXEC(xori);
OP_EXEC(ori);
OP_EXEC(andi);
OP_EXEC(xor);
OP_EXEC(or);
OP_EXEC(and);
OP_EXEC(slli);
OP_EXEC(srli);
OP_EXEC(srai);
OP_EXEC(sll);
OP_EXEC(srl);
OP_EXEC(sra);
OP_EXEC(slliw);
OP_EXEC(srliw);
OP_EXEC(sraiw);
OP_EXEC(sllw);
OP_EXEC(srlw);
OP_EXEC(sraw);
OP_EXEC(mul);
OP_EXEC(mulh);
OP_EXEC(mulhsu);
OP_EXEC(mulhu);
OP_EXEC(div);
OP_EXEC(divu);
OP_EXEC(rem);
OP_EXEC(remu);
OP_EXEC(mulw);
OP_EXEC(divw);
OP_EXEC(divuw);
OP_EXEC(remw);
OP_EXEC(remuw);
OP_EXEC(lb);
OP_EXEC(lh);
OP_EXEC(lw);
OP_EXEC(ld);
OP_EXEC(lbu);
OP_EXEC(lhu);
OP_EXEC(lwu);
OP_EXEC(sb);
OP_EXEC(sh);
OP_EXEC(sw);
OP_EXEC(sd);
OP_EXEC(beq);
OP_EXEC(bne);
OP_EXEC(blt);
OP_EXEC(bge);
OP_EXEC(bltu);
OP_EXEC(bgeu);
OP_EXEC(jal);
OP_EXEC(jalr);
OP_EXEC(fence);
OP_EXEC(fence_i);
OP_EXEC(ecall);
OP_EXEC(ebreak);
OP_EXEC(mret);
OP_EXEC(wfi);
OP_EXEC(csrrw);
OP_EXEC(csrrs);
OP_EXEC(csrrc);
OP_EXEC(csrrwi);
OP_EXEC(csrrsi);
OP_EXEC(csrrci);

/* Decode-cache specialisations. */
OP_EXEC(discard);
OP_EXEC(li);
OP_EXEC(mv);

int op_exec_advance(TargetCoreType *core, const RiscvDecodedInsn *decoded);
int op_exec_memory_fault(TargetCoreType *core, uint64 address, uint64 cause);
uint64 op_exec_sign_extend(uint64 value, uint32 width);
uint64 op_exec_sign_extend_word(uint32 value);

#endif
