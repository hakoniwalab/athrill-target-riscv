#ifndef ATHRILL_RISCV_OP_EXEC_FPU_INTERNAL_H
#define ATHRILL_RISCV_OP_EXEC_FPU_INTERNAL_H

#include "op_exec.h"

#define FFLAG_NX 1U
#define FFLAG_UF 2U
#define FFLAG_OF 4U
#define FFLAG_DZ 8U
#define FFLAG_NV 16U
#define FFLAG_INPUT_DENORM 0x80000000U
#define CANONICAL_NAN 0x7fc00000U

uint32 float_bits(float value);
float bits_float(uint32 bits);
std_bool is_nan_bits(uint32 bits);
std_bool is_snan_bits(uint32 bits);
int fp_illegal(TargetCoreType *core, const RiscvDecodedInsn *decoded);
std_bool fp_enabled(TargetCoreType *core);
void fp_dirty(TargetCoreType *core);
uint32 fp_read(TargetCoreType *core, uint8 reg);
void fp_write(TargetCoreType *core, uint8 reg, uint32 bits);
int set_rounding(TargetCoreType *core, const RiscvDecodedInsn *decoded);
int fp_finish(TargetCoreType *core, const RiscvDecodedInsn *decoded, uint32 bits);
uint32 rmm_adjust(double exact_high, double exact_low, uint32 rounded_bits);

/*
 * The host's floating-point exception flags around one guest operation:
 * fp_host_clear_flags() before it, fp_host_flags() after it gives the
 * flags it raised as fflags bits. On AArch64 they are the FPSR's
 * cumulative bits, read and written directly (the libm calls cost more
 * than the operation); elsewhere <fenv.h>. Called only around volatile
 * float operations, which keeps the order.
 */
#if defined(__aarch64__)
#define FP_HOST_FPSR_FLAGS 0x9fULL	/* IOC DZC OFC UFC IXC, IDC */

static inline void fp_host_clear_flags(void)
{
	uint64 fpsr;

	__asm__ volatile("mrs %0, fpsr" : "=r"(fpsr) : : "memory");
	fpsr &= ~FP_HOST_FPSR_FLAGS;
	__asm__ volatile("msr fpsr, %0" : : "r"(fpsr) : "memory");
}

static inline uint32 fp_host_flags(void)
{
	uint64 fpsr;
	uint32 flags = 0U;

	__asm__ volatile("mrs %0, fpsr" : "=r"(fpsr) : : "memory");
	if ((fpsr & 0x10U) != 0U) {	/* IXC */
		flags |= FFLAG_NX;
	}
	if ((fpsr & 0x08U) != 0U) {	/* UFC */
		flags |= FFLAG_UF;
	}
	if ((fpsr & 0x04U) != 0U) {	/* OFC */
		flags |= FFLAG_OF;
	}
	if ((fpsr & 0x02U) != 0U) {	/* DZC */
		flags |= FFLAG_DZ;
	}
	if ((fpsr & 0x01U) != 0U) {	/* IOC */
		flags |= FFLAG_NV;
	}
	return flags;
}
#else
#include <fenv.h>

static inline void fp_host_clear_flags(void)
{
	feclearexcept(FE_ALL_EXCEPT);
}

static inline uint32 fp_host_flags(void)
{
	int exceptions = fetestexcept(FE_ALL_EXCEPT);
	uint32 flags = 0U;

	if ((exceptions & FE_INEXACT) != 0) {
		flags |= FFLAG_NX;
	}
	if ((exceptions & FE_UNDERFLOW) != 0) {
		flags |= FFLAG_UF;
	}
	if ((exceptions & FE_OVERFLOW) != 0) {
		flags |= FFLAG_OF;
	}
	if ((exceptions & FE_DIVBYZERO) != 0) {
		flags |= FFLAG_DZ;
	}
	if ((exceptions & FE_INVALID) != 0) {
		flags |= FFLAG_NV;
	}
	return flags;
}
#endif

#endif
