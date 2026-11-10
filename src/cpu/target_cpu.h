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
    uint32 instruction;	/* the base instruction (compressed ones expanded) */
    uint32 raw;	/* the instruction bits in memory, compared on a cache hit */
    RiscvDecodedExecutor execute;
    uint8 *code;
    /*
     * Fetch permission is checked when the entry is filled. A cached entry is
     * reused only under the same PMP setting and privilege it was checked with.
     */
    uint64 pmp_epoch;
    RiscvPrivilege privilege;
    uint8 rd, rs1, rs2, rs3, length, aux, valid;
    /*
     * The straight-line block that starts here (cpu_config.c): its length in
     * instructions (0: none yet) and the page generation it was built in.
     */
    uint8 block_length;
    uint32 block_generation;
#ifdef ATHRILL_ENABLE_JIT
    /* The JIT code of the block that starts here, valid for these values (cpu_config.c). */
    const void *jit_code;
    uint64 jit_generation;
    uint64 jit_epoch;
#endif
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

/*
 * The address ranges of the active PMP entries, worked out once per PMP
 * setting (pmp_epoch) instead of on every access.
 */
typedef struct {
    std_bool valid;
    uint64 epoch;
    uint8 count;
    uint8 cfg[16];
    uint64 lo[16];
    uint64 hi[16];
} RiscvPmpTable;

/*
 * Decode cache: an array of decoded instructions for each 4 KiB page of
 * code (one entry per halfword), allocated when the page is first run and
 * found through a directory indexed by the page number. The directory
 * covers 16 MiB of code without two pages sharing a slot; pages further
 * apart replace each other.
 */
#define RISCV_DECODE_PAGE_SHIFT 12U
#define RISCV_DECODE_PAGE_ENTRIES (1U << (RISCV_DECODE_PAGE_SHIFT - 1U))
#define RISCV_DECODE_DIRECTORY_SIZE 4096U
typedef struct RiscvDecodePage {
    uint64 base;
    /* Advanced whenever an entry is invalidated; drops the page's blocks. */
    uint32 generation;
    RiscvDecodedInsn entries[RISCV_DECODE_PAGE_ENTRIES];
} RiscvDecodePage;

/*
 * The ROM/RAM region a core accessed last, for direct loads and stores, and
 * whether PMP allows reads and writes of all of it, worked out for one PMP
 * setting and privilege (pmp_valid is cleared when the window moves).
 */
typedef struct {
    std_bool valid;
    std_bool writable;
    uint64 start;
    uint64 end;
    uint8 *data;
    std_bool pmp_valid, pmp_read, pmp_write;
    uint64 pmp_epoch;
    RiscvPrivilege pmp_privilege;
} RiscvMemoryWindow;

typedef struct TargetCoreTypeTag {
    uint32 real_elaps;
    CoreIdType core_id;
    CpuRegisterType reg;
    std_bool is_halt;
    uint16 *current_code;
    OpDecodedCodeType *decoded_code;
    std_bool access_fault;
    uint64 internal_cycle, internal_instret, pmp_epoch;
    RiscvPmpTable pmp_table;
    RiscvMemoryWindow memory_window;
    uint64 trace_store_address, trace_store_value;
    uint8 trace_store_size, trace_store_valid;
    /* Decoded instructions, one page per 4 KiB of code (riscv_decode.h). */
    struct RiscvDecodePage *decode_pages[RISCV_DECODE_DIRECTORY_SIZE];
    /*
     * The addresses that have held decode pages since the last clear:
     * [decode_lo, decode_hi), only growing (0, 0: none). A store outside
     * it cannot overwrite a decoded instruction (riscv_decode_slot()).
     */
    uint64 decode_lo, decode_hi;
} TargetCoreType;

static inline AthrillAddrType cpu_get_pc(const TargetCoreType *core) { return core->reg.pc; }
static inline uint32 cpu_get_sp(const TargetCoreType *core) { return (uint32)core->reg.x[CPU_REG_SP]; }

void riscv_idle_spin_halt_init(void);

#endif
