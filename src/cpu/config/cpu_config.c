#include "cpu.h"
#include "bus.h"
#include "std_cpu_ops.h"
#include "mpu_types.h"
#include "riscv_decode.h"
#include "pmp.h"
#include <string.h>
#include <inttypes.h>
#include <stdio.h>

CpuType virtual_cpu;
uint32 riscv_trace_enabled;
static uint64 riscv_trace_sequence;

static void reset_core(TargetCoreType *core)
{
    CoreIdType id = core->core_id;
    memset(core, 0, sizeof(*core));
    core->core_id = id;
    core->reg.privilege = RISCV_PRIV_M;
    core->reg.csr.misa = (2ULL << 62) | (1ULL << ('I'-'A')) | (1ULL << ('M'-'A')) | (1ULL << ('C'-'A')) | (1ULL << ('F'-'A'));
    core->reg.csr.mhartid = id;
}

void cpu_init(void)
{
    CoreIdType i;
    riscv_decode_init();
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
    if ((core->reg.pc & 1U) != 0) {
        riscv_raise_exception(core, 0, core->reg.pc); core->access_fault = TRUE; return STD_E_OK;
    }
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
    RiscvDecodedInsn *decoded;
    uint32 insn;
    uint8 length;
    int retired = RISCV_EXEC_TRAP;
    uint64 pending = core->reg.csr.mip & core->reg.csr.mie;
    uint64 trace_pc = core->reg.pc;
    uint64 trace_mstatus = core->reg.csr.mstatus;
    uint64 trace_mepc = core->reg.csr.mepc;
    uint64 trace_mcause = core->reg.csr.mcause;
    core->reg.x[0] = 0;
    core->access_fault = FALSE;
    if (pending != 0 && (core->reg.privilege != RISCV_PRIV_M || (core->reg.csr.mstatus & (1ULL << 3)) != 0)) {
        unsigned cause = (pending & (1ULL << 11)) ? 11U : (pending & (1ULL << 7)) ? 7U : 3U;
        riscv_raise_interrupt(core, cause);
        core->is_halt = FALSE;
        goto clock_done;
    }
    decoded = &core->decode_cache[(core->reg.pc >> 1) & (RISCV_DECODE_CACHE_ENTRIES - 1U)];
    if (!core->is_halt && decoded->valid && decoded->pc == core->reg.pc &&
        decoded->pmp_epoch == core->pmp_epoch) {
        insn = (uint32)decoded->code[0] | (uint32)decoded->code[1] << 8;
        if (decoded->length == 4U) insn |= (uint32)decoded->code[2] << 16 | (uint32)decoded->code[3] << 24;
        else if (!riscv_expand_compressed((uint16)insn, &insn)) insn = 0;
        if (insn == decoded->instruction) {
            retired = decoded->execute(core, decoded);
        }
        else if (riscv_decode_fill(core->reg.pc, insn, decoded->length, decoded)) {
            decoded->pmp_epoch = core->pmp_epoch;
            retired = decoded->execute(core, decoded);
        }
        else {
            core->decoded_code = NULL;
            riscv_raise_exception(core, 2, insn);
        }
    }
    else if (!core->is_halt && fetch(core, &insn, &length) == STD_E_OK && !core->access_fault) {
        uint8 *code = NULL;
        uint32 expanded = insn;
        if ((length == 4U || riscv_expand_compressed((uint16)insn, &expanded)) &&
            riscv_decode_fill(core->reg.pc, expanded, length, decoded) &&
            bus_get_pointer(core->core_id, core->reg.pc, &code) == STD_E_OK) {
            decoded->code = code;
            decoded->pmp_epoch = core->pmp_epoch;
            retired = decoded->execute(core, decoded);
        }
        else {
            decoded->valid = 0;
            core->decoded_code = NULL;
            riscv_raise_exception(core, 2, insn);
        }
    }
clock_done:
    if (riscv_trace_enabled && !core->is_halt) {
        printf("I hart=%u seq=%" PRIu64 " prv=%u pc=%016" PRIx64 " insn=%08x\n", (unsigned)id, riscv_trace_sequence++, (unsigned)core->reg.privilege, trace_pc, insn);
        if (decoded != NULL && decoded->rd != 0U && retired == RISCV_EXEC_RETIRED) printf("X hart=%u reg=%u value=%016" PRIx64 "\n", (unsigned)id, (unsigned)decoded->rd, core->reg.x[decoded->rd]);
        if (core->reg.csr.mstatus != trace_mstatus) printf("C hart=%u csr=300 value=%016" PRIx64 "\n", (unsigned)id, core->reg.csr.mstatus);
        if (core->reg.csr.mepc != trace_mepc) printf("C hart=%u csr=341 value=%016" PRIx64 "\n", (unsigned)id, core->reg.csr.mepc);
        if (core->reg.csr.mcause != trace_mcause) printf("T hart=%u cause=%016" PRIx64 " tval=%016" PRIx64 " epc=%016" PRIx64 "\n", (unsigned)id, core->reg.csr.mcause, core->reg.csr.mtval, core->reg.csr.mepc);
    }
    core->internal_cycle++;
    if (retired == RISCV_EXEC_RETIRED) core->internal_instret++;
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
void intc_cpu_trigger_interrupt(CoreIdType id, int intno)
{
    TargetCoreType *core = &virtual_cpu.cores[id].core;
    if (intno >= 0 && intno < 64) core->reg.csr.mip |= 1ULL << (unsigned)intno;
    if ((core->reg.csr.mip & core->reg.csr.mie) != 0) core->is_halt = FALSE;
}
