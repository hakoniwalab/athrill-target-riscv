#include "cpu.h"
#include "bus.h"
#include "std_cpu_ops.h"
#include "mpu_types.h"
#include "riscv_decode.h"
#include "pmp.h"
#include <string.h>

CpuType virtual_cpu;

static void reset_core(TargetCoreType *core)
{
    CoreIdType id = core->core_id;
    memset(core, 0, sizeof(*core));
    core->core_id = id;
    core->reg.privilege = RISCV_PRIV_M;
    core->reg.csr.misa = (2ULL << 62) | (1ULL << ('I'-'A')) |
        (1ULL << ('M'-'A')) | (1ULL << ('F'-'A')) | (1ULL << ('C'-'A'));
    core->reg.csr.mhartid = id;
}

void cpu_init(void)
{
    CoreIdType i;
    for (i = 0; i < cpu_config_get_core_id_num(); i++) {
        virtual_cpu.cores[i].core.core_id = i;
        cpu_reset(i);
    }
}
void cpu_reset(CoreIdType id) { reset_core(&virtual_cpu.cores[id].core); }
void cpu_set_current_core(CoreIdType id) { virtual_cpu.current_core = &virtual_cpu.cores[id]; }
std_bool cpu_is_halt(CoreIdType id) { return virtual_cpu.cores[id].core.is_halt; }
std_bool cpu_is_halt_all(void) { return cpu_is_halt(0); }
void cpu_mpu_construct_containers(CoreIdType id) { (void)id; }

static Std_ReturnType fetch(TargetCoreType *core, uint32 *instruction, uint8 *length)
{
    uint8 *p;
    uint16 lo;
    if (!riscv_pmp_check(core, core->reg.pc, 2, CpuMemoryAccess_EXEC)) {
        riscv_raise_exception(core, 1, core->reg.pc); core->access_fault = TRUE; return STD_E_OK;
    }
    if (bus_get_pointer(core->core_id, core->reg.pc, &p) != STD_E_OK) {
        riscv_raise_exception(core, 1, core->reg.pc); core->access_fault = TRUE; return STD_E_OK;
    }
    lo = (uint16)p[0] | ((uint16)p[1] << 8);
    *length = ((lo & 3U) == 3U) ? 4U : 2U;
    *instruction = lo;
    if (*length == 4U) {
        if (!riscv_pmp_check(core, core->reg.pc, 4, CpuMemoryAccess_EXEC)) {
            riscv_raise_exception(core, 1, core->reg.pc); core->access_fault = TRUE; return STD_E_OK;
        }
        *instruction |= (uint32)p[2] << 16 | (uint32)p[3] << 24;
    }
    return STD_E_OK;
}

Std_ReturnType cpu_supply_clock(CoreIdType id)
{
    TargetCoreType *core = &virtual_cpu.cores[id].core;
    uint32 insn;
    uint8 length;
    uint32 i;
    if (core->is_halt) return STD_E_OK;
    core->reg.x[0] = 0;
    core->access_fault = FALSE;
    if (fetch(core, &insn, &length) != STD_E_OK || core->access_fault) return STD_E_OK;
    for (i = 0; i < riscv_dispatch_table_count; i++) {
        if ((insn & riscv_dispatch_table[i].mask) == riscv_dispatch_table[i].match)
            return (Std_ReturnType)riscv_dispatch_table[i].execute(core, insn);
    }
    core->decoded_code = NULL;
    riscv_raise_exception(core, 2, insn);
    core->reg.csr.mcycle++;
    core->reg.x[0] = 0;
    virtual_cpu.cores[id].elaps++;
    return STD_E_OK;
}

void cpu_illegal_opcode_trap(CoreIdType id) { riscv_raise_exception(&virtual_cpu.cores[id].core, 2, 0); }
std_bool cpu_illegal_access(CoreIdType id) { std_bool v = virtual_cpu.cores[id].core.access_fault; virtual_cpu.cores[id].core.access_fault = FALSE; return v; }
std_bool cpu_has_permission(CoreIdType id, MpuAddressRegionEnumType region, CpuMemoryAccessType access, AthrillAddrType addr, AthrillAddrType size)
{ (void)region; return riscv_pmp_check(&virtual_cpu.cores[id].core, addr, size, access); }

uint32 cpu_get_current_core_id(void) { return virtual_cpu.current_core ? virtual_cpu.current_core->core.core_id : 0; }
uint32 cpu_get_ep(const TargetCoreType *core) { (void)core; return 0; }
uint32 cpu_get_current_core_register(uint32 i) { return (uint32)virtual_cpu.current_core->core.reg.x[i]; }
AthrillAddrType cpu_get_current_core_pc(void) { return virtual_cpu.current_core->core.reg.pc; }
void cpu_set_core_pc(CoreIdType id, AthrillAddrType pc) { virtual_cpu.cores[id].core.reg.pc = pc; }
uint32 cpu_get_current_core_sp(void) { return (uint32)virtual_cpu.current_core->core.reg.x[2]; }
uint32 cpu_get_current_core_ep(void) { return 0; }
AthrillAddrType cpu_get_return_addr(const TargetCoreType *core) { return core->reg.x[1]; }
CoreIdType cpu_get_core_id(const TargetCoreType *core) { return core->core_id; }
void intc_cpu_trigger_interrupt(CoreIdType id, int intno) { (void)id; (void)intno; }
