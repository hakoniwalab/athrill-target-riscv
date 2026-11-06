#include "cpu.h"
#include "cpuemu_ops.h"
#include "bus.h"
#include "std_cpu_ops.h"
#include "mpu_types.h"
#include "riscv_decode.h"
#include "pmp.h"
#include "intc.h"
#include "erbium_devices.h"
#include "athrill_idle_spin_halt.h"
#include <string.h>
#include <inttypes.h>
#include <stdio.h>

CpuType virtual_cpu;
uint32 riscv_trace_enabled;
static uint64 riscv_trace_sequence;

void riscv_trace_store(
    TargetCoreType *core,
    uint64 address,
    uint8 size,
    uint64 value)
{
	core->trace_store_address = address;
	core->trace_store_size = size;
	core->trace_store_value = (size < 8U) ?
	                          (value & ((1ULL << (size * 8U)) - 1U)) : value;
	core->trace_store_valid = TRUE;
}

static void reset_core(TargetCoreType *core)
{
	CoreIdType id = core->core_id;
	riscv_decode_cache_clear(core);
	memset(core, 0, sizeof(*core));
	core->core_id = id;
	core->reg.privilege = RISCV_PRIV_M;
	core->reg.csr.misa = (2ULL << 62) | (1ULL << ('I' - 'A')) | (1ULL << ('M' - 'A')) |
	                     (1ULL << ('C' - 'A')) | (1ULL << ('F' - 'A'));
	if (CPU_CONFIG_ERBIUM_COMPAT) {
		core->reg.csr.mstatus = 0x0000000a00001800ULL;
	}
	core->reg.csr.mhartid = id;
}

void cpu_init(void)
{
	CoreIdType i;
	riscv_idle_spin_halt_init();
	riscv_decode_init();
	for (i = 0; i < cpu_config_get_core_id_num(); i++) {
		virtual_cpu.cores[i].core.core_id = i;
		cpu_reset(i);
	}
}
void cpu_reset(CoreIdType id)
{
	reset_core(&virtual_cpu.cores[id].core);
}
void cpu_set_current_core(CoreIdType id)
{
	virtual_cpu.current_core = &virtual_cpu.cores[id];
}
std_bool cpu_is_halt(CoreIdType id)
{
	return virtual_cpu.cores[id].core.is_halt;
}
std_bool cpu_is_halt_all(void)
{
	return cpu_is_halt(0);
}
void cpu_mpu_construct_containers(CoreIdType id)
{
	(void)id;
}

static Std_ReturnType fetch(TargetCoreType *core, uint32 *instruction, uint8 *length,
	std_bool *cacheable)
{
	uint8 *p;
	uint8 *upper;
	uint16 lo;
	if ((core->reg.pc & 1U) != 0) {
		riscv_raise_exception(core, 0, core->reg.pc);
		core->access_fault = TRUE;
		return STD_E_OK;
	}
	if (!riscv_pmp_check(core, core->reg.pc, 2, CpuMemoryAccess_EXEC)) {
		riscv_raise_exception(core, 1, core->reg.pc);
		core->access_fault = TRUE;
		return STD_E_OK;
	}
	if (bus_get_pointer(core->core_id, core->reg.pc, &p) != STD_E_OK) {
		riscv_raise_exception(core, 1, core->reg.pc);
		core->access_fault = TRUE;
		return STD_E_OK;
	}
	lo = (uint16)p[0] | ((uint16)p[1] << 8);
	*cacheable = TRUE;
	*length = ((lo & 3U) == 3U) ? 4U : 2U;
	*instruction = lo;
	if (*length == 4U) {
		if (!riscv_pmp_check(core, core->reg.pc, 4, CpuMemoryAccess_EXEC)) {
			riscv_raise_exception(core, 1, core->reg.pc);
			core->access_fault = TRUE;
			return STD_E_OK;
		}
		/*
		 * The upper half is fetched through its own pointer: Athrill keeps
		 * each memory region in its own buffer, so an instruction may span two
		 * regions. Unmapped upper half: instruction access fault.
		 */
		if (bus_get_pointer(core->core_id, core->reg.pc + 2U, &upper) != STD_E_OK) {
			riscv_raise_exception(core, 1, core->reg.pc + 2U);
			core->access_fault = TRUE;
			return STD_E_OK;
		}
		/* The decode cache re-reads all four bytes through one pointer. */
		*cacheable = (upper == p + 2);
		*instruction |= (uint32)upper[0] << 16 | (uint32)upper[1] << 24;
	}
	return STD_E_OK;
}

static std_bool decode_cache_hit(const TargetCoreType *core, const RiscvDecodedInsn *decoded, uint64 pc)
{
	return decoded->valid
		&& decoded->pc == pc
		&& decoded->pmp_epoch == core->pmp_epoch
		&& decoded->privilege == core->reg.privilege;
}

static void decode_cache_remember_permission(const TargetCoreType *core, RiscvDecodedInsn *decoded)
{
	decoded->pmp_epoch = core->pmp_epoch;
	decoded->privilege = core->reg.privilege;
}

Std_ReturnType cpu_supply_clock(CoreIdType id)
{
	TargetCoreType *core = &virtual_cpu.cores[id].core;
	RiscvDecodedInsn *decoded;
	uint32 insn = 0U;	/* stays 0 in the trace when no instruction is fetched */
	uint32 raw;
	std_bool use_entry;
	uint8 length;
	std_bool cacheable;
	int retired = RISCV_EXEC_TRAP;
	uint32 interrupt_cause;
	uint64 trace_pc = core->reg.pc;
	CpuRegisterType trace_registers;
	uint32 trace_index;

	/* The register snapshot is only needed to print the trace deltas. */
	if (riscv_trace_enabled) {
		trace_registers = core->reg;
	}
	core->reg.x[0] = 0;
	core->access_fault = FALSE;
	core->trace_store_valid = FALSE;
	if (intc_take_interrupt(core, &interrupt_cause)) {
		riscv_raise_interrupt(core, interrupt_cause);
		core->is_halt = FALSE;
		goto clock_done;
	}
	if (athrill_idle_spin_halt_before_execute(id)) {
		goto clock_done;
	}
	decoded = riscv_decode_lookup(core, core->reg.pc);
	use_entry = !core->is_halt && (decoded != NULL) && decode_cache_hit(core, decoded, core->reg.pc);
	if (use_entry) {
		/*
		 * Reuse the entry only while the bytes in memory are unchanged;
		 * modified code (any length) is fetched and decoded again below.
		 */
		raw = (uint32)decoded->code[0] | (uint32)decoded->code[1] << 8;
		if (decoded->length == 4U) raw |= (uint32)decoded->code[2] << 16 | (uint32)decoded->code[3] << 24;
		use_entry = (raw == decoded->raw);
	}
	if (use_entry) {
		insn = decoded->instruction;
		retired = decoded->execute(core, decoded);
	} else if (!core->is_halt && fetch(core, &insn, &length, &cacheable) == STD_E_OK
		&& !core->access_fault) {
		uint8 *code = NULL;
		uint32 expanded = insn;
		decoded = riscv_decode_slot(core, core->reg.pc);
		if ((decoded != NULL) &&
		    (length == 4U || riscv_expand_compressed((uint16)insn, &expanded)) &&
		    riscv_decode_fill(core->reg.pc, expanded, length, decoded) &&
		    bus_get_pointer(core->core_id, core->reg.pc, &code) == STD_E_OK) {
			decoded->code = code;
			decoded->raw = insn;
			decode_cache_remember_permission(core, decoded);
			retired = decoded->execute(core, decoded);
			if (!cacheable) {
				decoded->valid = 0;
			}
		} else {
			if (decoded != NULL) {
				decoded->valid = 0;
			}
			core->decoded_code = NULL;
			riscv_raise_exception(core, 2, insn);
		}
	}
	if (!core->is_halt && (core->reg.pc == trace_pc)) {
		athrill_idle_spin_halt_after_execute(id, trace_pc);
	}
clock_done:
	if (riscv_trace_enabled && !core->is_halt) {
		/* clk is the virtual clock, so interrupt and task-switch times can be compared. */
		printf("I hart=%u seq=%" PRIu64 " clk=%" PRIu64 " prv=%u pc=%016" PRIx64 " insn=%08x\n",
		       (unsigned)id, riscv_trace_sequence++, cpuemu_get_clock(),
		       (unsigned)core->reg.privilege, trace_pc, insn);
		for (trace_index = 1U; trace_index < 32U; trace_index++) {
			if (core->reg.x[trace_index] != trace_registers.x[trace_index]) printf("X hart=%u reg=%u value=%016"
				    PRIx64 "\n", (unsigned)id, trace_index, core->reg.x[trace_index]);
			if (core->reg.f[trace_index] != trace_registers.f[trace_index]) printf("F hart=%u reg=%u value=%016"
				        PRIx64 "\n", (unsigned)id, trace_index, core->reg.f[trace_index]);
		}
		if (core->reg.f[0] != trace_registers.f[0]) printf("F hart=%u reg=0 value=%016" PRIx64 "\n",
			        (unsigned)id, core->reg.f[0]);
#define TRACE_CSR(number, member) if (core->reg.csr.member != trace_registers.csr.member) printf("C hart=%u csr=%03x value=%016" PRIx64 "\n", (unsigned)id, number, core->reg.csr.member)
		TRACE_CSR(0x300U, mstatus);
		TRACE_CSR(0x301U, misa);
		TRACE_CSR(0x304U, mie);
		TRACE_CSR(0x305U, mtvec);
		TRACE_CSR(0x340U, mscratch);
		TRACE_CSR(0x341U, mepc);
		TRACE_CSR(0x342U, mcause);
		TRACE_CSR(0x343U, mtval);
		TRACE_CSR(0x344U, mip);
		TRACE_CSR(0x302U, medeleg);
		TRACE_CSR(0x303U, mideleg);
		TRACE_CSR(0x306U, mcounteren);
		TRACE_CSR(0x30aU, menvcfg);
		TRACE_CSR(0x100U, sstatus);
		TRACE_CSR(0x104U, sie);
		TRACE_CSR(0x105U, stvec);
		TRACE_CSR(0x106U, scounteren);
		TRACE_CSR(0x140U, sscratch);
		TRACE_CSR(0x141U, sepc);
		TRACE_CSR(0x142U, scause);
		TRACE_CSR(0x143U, stval);
		TRACE_CSR(0x144U, sip);
		TRACE_CSR(0x180U, satp);
#undef TRACE_CSR
		if (core->reg.fcsr != trace_registers.fcsr) printf("C hart=%u csr=003 value=%016" PRIx64 "\n",
			        (unsigned)id, (uint64)core->reg.fcsr);
		if (core->trace_store_valid) printf("W hart=%u addr=%016" PRIx64 " size=%u value=%016" PRIx64 "\n",
			                                    (unsigned)id, core->trace_store_address, (unsigned)core->trace_store_size, core->trace_store_value);
		if (core->reg.csr.mcause != trace_registers.csr.mcause) printf("T hart=%u cause=%016" PRIx64
			        " tval=%016" PRIx64 " epc=%016" PRIx64 "\n", (unsigned)id, core->reg.csr.mcause,
			        core->reg.csr.mtval, core->reg.csr.mepc);
	}
	core->internal_cycle++;
	if (retired == RISCV_EXEC_RETIRED) core->internal_instret++;
	core->reg.x[0] = 0;
	virtual_cpu.cores[id].elaps++;
	return STD_E_OK;
}

/* The instruction bytes in memory are still the ones the entry was decoded from. */
static inline std_bool code_unchanged(const RiscvDecodedInsn *decoded)
{
	uint32 raw = (uint32)decoded->code[0] | (uint32)decoded->code[1] << 8;

	if (decoded->length == 4U) {
		raw |= (uint32)decoded->code[2] << 16 | (uint32)decoded->code[3] << 24;
	}
	return raw == decoded->raw;
}

/*
 * Batched execution (ATHRILL_TARGET_BATCH_CLOCKS, see std_cpu_ops.h).
 *
 * A batch continues unless the CPU halted or the last instruction touched
 * a device register (which may have moved a device event).
 */
static std_bool batch_can_continue(const TargetCoreType *core)
{
	if (core->is_halt) {
		return FALSE;
	}
	if (riscv_device_accessed) {
		riscv_device_accessed = FALSE;
		return FALSE;
	}
	return TRUE;
}

/*
 * The common case of cpu_supply_clock(), without its general handling:
 * no interrupt pending, the instruction in the decode cache with its bytes
 * unchanged, and no trace. Returns FALSE (nothing done) otherwise, and the
 * caller runs cpu_supply_clock().
 */
static inline std_bool execute_cached(TargetCoreType *core, CoreIdType id)
{
	RiscvDecodedInsn *decoded;
	uint64 pc = core->reg.pc;
	int retired;

	if ((core->reg.csr.mip & core->reg.csr.mie) != 0U) {
		return FALSE;
	}
	decoded = riscv_decode_lookup(core, core->reg.pc);
	if ((decoded == NULL) || !decode_cache_hit(core, decoded, core->reg.pc) || !code_unchanged(decoded)) {
		return FALSE;
	}
	core->access_fault = FALSE;
	retired = decoded->execute(core, decoded);
	if (core->reg.pc == pc) {
		athrill_idle_spin_halt_after_execute(id, pc);
	}
	core->internal_cycle++;
	if (retired == RISCV_EXEC_RETIRED) {
		core->internal_instret++;
	}
	core->reg.x[0] = 0;
	virtual_cpu.cores[id].elaps++;
	return TRUE;
}

/*
 * Straight-line blocks. A cached entry can start a block: the cached
 * instructions from it up to and including the first jump or instruction
 * that may change the machine state (jal, jalr; SYSTEM: CSR access, ecall,
 * mret, wfi; MISC-MEM: fence), all in one decode page. A conditional branch
 * does not end a block: taken, it leaves the pc off the next entry, which
 * ends the block like a trap does; not taken, the block goes on. Each entry
 * of a block is checked like execute_cached() checks one when the block is
 * built, so running it needs no check per instruction. Every store into
 * code (riscv_decode_cache_invalidate) advances the page generation, which
 * drops the page's blocks; a refilled entry starts without one.
 *
 * Within a block nothing but the instructions changes the interrupt state:
 * CSR accesses end it, and a device register access ends it (and the batch)
 * right after the instruction, as in execute_cached(). An instruction that
 * traps leaves the pc off the next entry and ends it too. So a block gives
 * the same clocks and results as its instructions one by one.
 */
#define RISCV_BLOCK_MAX_LENGTH 64U

static std_bool ends_block(uint32 instruction)
{
	switch (instruction & 0x7fU) {
	case 0x6fU:	/* jal */
	case 0x67U:	/* jalr */
	case 0x73U:	/* SYSTEM */
	case 0x0fU:	/* MISC-MEM */
		return TRUE;
	default:
		return FALSE;
	}
}

static uint8 build_block(TargetCoreType *core, RiscvDecodePage *page, RiscvDecodedInsn *first)
{
	RiscvDecodedInsn *entry = first;
	uint64 pc = first->pc;
	uint8 length = 0U;

	while ((length < RISCV_BLOCK_MAX_LENGTH) && decode_cache_hit(core, entry, pc) && code_unchanged(entry)) {
		length++;
		if (ends_block(entry->instruction)) {
			break;
		}
		pc += entry->length;
		if (riscv_decode_page(core, pc) != page) {
			break;
		}
		entry = riscv_decode_page_entry(page, pc);
	}
	first->block_length = length;
	first->block_generation = page->generation;
	return length;
}

/*
 * Runs the block at the pc if it fits in budget clocks; returns the clocks
 * used (0: none run, the caller goes on one instruction at a time).
 */
static inline uint64 execute_block(TargetCoreType *core, CoreIdType id, uint64 budget, uint64 *clock)
{
	uint64 pc = core->reg.pc;
	RiscvDecodePage *page;
	RiscvDecodedInsn *entry;
	uint64 length;
	uint64 done = 0U;
	uint64 trapped = 0U;
	int retired;

	if ((core->reg.csr.mip & core->reg.csr.mie) != 0U) {
		return 0U;
	}
	page = riscv_decode_page(core, pc);
	if (page == NULL) {
		return 0U;
	}
	entry = riscv_decode_page_entry(page, pc);
	if (!decode_cache_hit(core, entry, pc)) {
		return 0U;
	}
	if ((entry->block_length == 0U) || (entry->block_generation != page->generation)) {
		(void)build_block(core, page, entry);
	}
	length = entry->block_length;
	if ((length == 0U) || (length > budget)) {
		return 0U;
	}
	/*
	 * Only CSR instructions read or write the cycle and instruction
	 * counters, and a CSR instruction can only be the last of a block. So
	 * the counters are brought up to date once, before the last
	 * instruction or when the block ends early, instead of after each
	 * instruction. A faulting instruction ends the block, so access_fault
	 * is cleared once.
	 */
	core->access_fault = FALSE;
	while (done + 1U < length) {
		if (athrill_idle_spin_halt_idle_ready(id)) {
			goto ended;
		}
		(*clock)++;
		retired = entry->execute(core, entry);
		done++;
		if (core->reg.pc == entry->pc) {
			athrill_idle_spin_halt_after_execute(id, entry->pc);
		}
		if (retired != RISCV_EXEC_RETIRED) {
			trapped++;
		}
		core->reg.x[0] = 0;
		if ((core->reg.pc != entry->pc + entry->length) || core->access_fault || riscv_device_accessed) {
			goto ended;
		}
		entry += entry->length >> 1U;
	}
	core->internal_cycle += done;
	core->internal_instret += done - trapped;
	if (athrill_idle_spin_halt_idle_ready(id)) {
		virtual_cpu.cores[id].elaps += done;
		return done;
	}
	(*clock)++;
	retired = entry->execute(core, entry);
	done++;
	if (core->reg.pc == entry->pc) {
		athrill_idle_spin_halt_after_execute(id, entry->pc);
	}
	core->internal_cycle++;
	if (retired == RISCV_EXEC_RETIRED) {
		core->internal_instret++;
	}
	core->reg.x[0] = 0;
	virtual_cpu.cores[id].elaps += done;
	return done;
ended:
	core->internal_cycle += done;
	core->internal_instret += done - trapped;
	virtual_cpu.cores[id].elaps += done;
	return done;
}

Std_ReturnType cpu_supply_clocks(CoreIdType core_id, uint64 max_clocks, uint64 *clock, uint64 *executed)
{
	TargetCoreType *core = &virtual_cpu.cores[core_id].core;
	Std_ReturnType err = STD_E_OK;
	uint64 n = 0U;
	uint64 done;

	while (n < max_clocks) {
		if (!batch_can_continue(core)) {
			break;
		}
		if (athrill_idle_spin_halt_idle_ready(core_id)) {
			(*clock)++;
			n++;
			err = cpu_supply_clock(core_id);
			if (err != STD_E_OK) {
				break;
			}
			continue;
		}
		if (!riscv_trace_enabled) {
			done = execute_block(core, core_id, max_clocks - n, clock);
			if (done != 0U) {
				n += done;
				continue;
			}
		}
		(*clock)++;
		n++;
		if (!riscv_trace_enabled && execute_cached(core, core_id)) {
			continue;
		}
		err = cpu_supply_clock(core_id);
		if (err != STD_E_OK) {
			break;
		}
	}
	*executed = n;
	return err;
}

void cpu_illegal_opcode_trap(CoreIdType id)
{
	riscv_raise_exception(&virtual_cpu.cores[id].core, 2, 0);
}
std_bool cpu_illegal_access(CoreIdType id)
{
	std_bool v = virtual_cpu.cores[id].core.access_fault;
	virtual_cpu.cores[id].core.access_fault = FALSE;
	return v;
}
std_bool cpu_has_permission(CoreIdType id, MpuAddressRegionEnumType region,
                            CpuMemoryAccessType access, AthrillAddrType addr, AthrillAddrType size)
{
	(void)region;
	return riscv_pmp_check(&virtual_cpu.cores[id].core, addr, size, access);
}

uint32 cpu_get_current_core_id(void)
{
	return virtual_cpu.current_core ? virtual_cpu.current_core->core.core_id : 0;
}
uint32 cpu_get_ep(const TargetCoreType *core)
{
	(void)core;
	return 0;
}
uint32 cpu_get_current_core_register(uint32 i)
{
	return (uint32)virtual_cpu.current_core->core.reg.x[i];
}
AthrillAddrType cpu_get_current_core_pc(void)
{
	return virtual_cpu.current_core->core.reg.pc;
}
void cpu_set_core_pc(CoreIdType id, AthrillAddrType pc)
{
	virtual_cpu.cores[id].core.reg.pc = pc;
}
uint32 cpu_get_current_core_sp(void)
{
	return (uint32)virtual_cpu.current_core->core.reg.x[2];
}
uint32 cpu_get_current_core_ep(void)
{
	return 0;
}
AthrillAddrType cpu_get_return_addr(const TargetCoreType *core)
{
	return core->reg.x[1];
}
CoreIdType cpu_get_core_id(const TargetCoreType *core)
{
	return core->core_id;
}
