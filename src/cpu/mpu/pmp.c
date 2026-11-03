#include "pmp.h"

#define PMP_R 0x01U
#define PMP_W 0x02U
#define PMP_X 0x04U
#define PMP_A_MASK 0x18U
#define PMP_A_TOR 0x08U
#define PMP_A_NA4 0x10U
#define PMP_A_NAPOT 0x18U
#define PMP_L 0x80U

static uint8 pmp_cfg(const TargetCoreType *core, unsigned index)
{
    return (uint8)(core->reg.csr.pmpcfg[index / 8U] >> ((index % 8U) * 8U));
}

static std_bool pmp_range(const TargetCoreType *core, unsigned i, uint64 *lo, uint64 *hi)
{
    uint8 cfg = pmp_cfg(core, i);
    uint64 a = core->reg.csr.pmpaddr[i];
    switch (cfg & PMP_A_MASK) {
    case PMP_A_TOR:
        *lo = i == 0 ? 0 : core->reg.csr.pmpaddr[i - 1] << 2;
        *hi = a << 2;
        return TRUE;
    case PMP_A_NA4:
        *lo = a << 2; *hi = *lo + 4; return TRUE;
    case PMP_A_NAPOT: {
        unsigned ones = 0;
        while (ones < 63 && ((a >> ones) & 1U) != 0) ones++;
        uint64 size = 1ULL << (ones + 3U);
        *lo = (a & ~((1ULL << ones) - 1U)) << 2;
        *hi = *lo + size;
        return TRUE;
    }
    default: return FALSE;
    }
}

static void pmp_table_build(TargetCoreType *core)
{
    RiscvPmpTable *table = &core->pmp_table;
    uint64 lo;
    uint64 hi;
    unsigned i;

    table->count = 0U;
    for (i = 0; i < 16; i++) {
        if (!pmp_range(core, i, &lo, &hi)) {
            continue;
        }
        table->cfg[table->count] = pmp_cfg(core, i);
        table->lo[table->count] = lo;
        table->hi[table->count] = hi;
        table->count++;
    }
    table->epoch = core->pmp_epoch;
    table->valid = TRUE;
}

/*
 * The first active entry (in priority order) that overlaps the access
 * decides; an access that only partly overlaps it fails, also in Machine
 * mode. Without an overlapping entry only Machine mode is allowed.
 * Every change of a PMP CSR advances pmp_epoch, which rebuilds the table.
 */
std_bool riscv_pmp_check(const TargetCoreType *core, uint64 addr, uint32 size, CpuMemoryAccessType access)
{
    TargetCoreType *mutable_core = (TargetCoreType *)core;	/* the table is a cache */
    const RiscvPmpTable *table = &core->pmp_table;
    uint64 end;
    unsigned i;
    uint8 cfg;

    if (size == 0 || addr > 0xffffffffffffffffULL - (size - 1U)) return FALSE;
    if (!table->valid || table->epoch != core->pmp_epoch) {
        pmp_table_build(mutable_core);
    }
    end = addr + size;
    for (i = 0; i < table->count; i++) {
        if (addr >= table->hi[i] || end <= table->lo[i]) continue;
        if (addr < table->lo[i] || end > table->hi[i]) return FALSE;
        cfg = table->cfg[i];
        if (core->reg.privilege == RISCV_PRIV_M && (cfg & PMP_L) == 0) return TRUE;
        if (access == CpuMemoryAccess_READ) return (cfg & PMP_R) != 0;
        if (access == CpuMemoryAccess_WRITE) return (cfg & PMP_W) != 0;
        if (access == CpuMemoryAccess_EXEC) return (cfg & PMP_X) != 0;
        return FALSE;
    }
    return core->reg.privilege == RISCV_PRIV_M;
}

void riscv_raise_exception(TargetCoreType *core, uint64 cause, uint64 tval)
{
    uint64 old = core->reg.csr.mstatus;
    core->reg.csr.mepc = core->reg.pc;
    core->reg.csr.mcause = cause;
    core->reg.csr.mtval = tval;
    old = (old & ~(3ULL << 11)) | ((uint64)core->reg.privilege << 11);
    if (old & (1ULL << 3)) old |= 1ULL << 7; else old &= ~(1ULL << 7);
    old &= ~(1ULL << 3);
    core->reg.csr.mstatus = old;
    core->reg.privilege = RISCV_PRIV_M;
    core->reg.pc = core->reg.csr.mtvec & ~3ULL;
}

void riscv_raise_interrupt(TargetCoreType *core, uint64 cause)
{
    riscv_raise_exception(core, (1ULL << 63) | cause, 0);
    if ((core->reg.csr.mtvec & 3U) == 1U)
        core->reg.pc = (core->reg.csr.mtvec & ~3ULL) + 4U * cause;
}
