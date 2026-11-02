#ifndef ATHRILL_RISCV_TARGET_CPU_H
#define ATHRILL_RISCV_TARGET_CPU_H

#include "std_types.h"

#define ATHRILL_TARGET_ARCH "RV64IMFC"
#define ATHRILL_TARGET_VERSION "0.1.0"
#define CPU_REG_SP 2U
#define CPU_REG_LP 1U

typedef enum { RISCV_PRIV_U = 0, RISCV_PRIV_S = 1, RISCV_PRIV_M = 3 } RiscvPrivilege;
typedef enum { CpuMemoryAccess_NONE = 0, CpuMemoryAccess_READ, CpuMemoryAccess_WRITE, CpuMemoryAccess_EXEC } CpuMemoryAccessType;
typedef enum { OpCodeId_Illegal = 0, OpCodeId_Num } OpCodeId;
typedef struct { uint32 instruction; uint8 length; } OpDecodedCodeType;

struct TargetCoreTypeTag;
struct RiscvDecodedInsn;
typedef int (*RiscvDecodedExecutor)(struct TargetCoreTypeTag *, const struct RiscvDecodedInsn *);
typedef struct RiscvDecodedInsn {
    uint64 pc, immediate;
    uint32 instruction;
    RiscvDecodedExecutor execute;
    uint8 *code;
    uint64 pmp_epoch;
    uint8 rd, rs1, rs2, length, aux, valid;
} RiscvDecodedInsn;

typedef struct {
    uint64 mstatus, misa, mhartid, mtvec, mepc, mcause, mtval, mie, mip, mscratch;
    uint64 medeleg, mideleg, mcounteren, menvcfg;
    uint64 mcycle, minstret;
    uint64 sstatus, sie, stvec, scounteren, sscratch, sepc, scause, stval, sip, satp;
    uint64 pmpcfg[4];
    uint64 pmpaddr[16];
} RiscvCsrType;

typedef struct {
    uint64 x[32];
    uint64 pc;
    uint64 f[32];
    uint32 fcsr;
    RiscvPrivilege privilege;
    RiscvCsrType csr;
} CpuRegisterType;

typedef struct TargetCoreTypeTag {
    uint32 real_elaps;
    CoreIdType core_id;
    CpuRegisterType reg;
    std_bool is_halt;
    uint16 *current_code;
    OpDecodedCodeType *decoded_code;
    std_bool access_fault;
    uint64 internal_cycle, internal_instret, pmp_epoch;
#define RISCV_DECODE_CACHE_ENTRIES 2048U
    RiscvDecodedInsn decode_cache[RISCV_DECODE_CACHE_ENTRIES];
} TargetCoreType;

static inline AthrillAddrType cpu_get_pc(const TargetCoreType *core) { return core->reg.pc; }
static inline uint32 cpu_get_sp(const TargetCoreType *core) { return (uint32)core->reg.x[CPU_REG_SP]; }

#endif
