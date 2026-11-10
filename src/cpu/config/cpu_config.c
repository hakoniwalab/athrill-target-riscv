#include "cpu.h"
#include "cpuemu_ops.h"
#include "bus.h"
#include "std_cpu_ops.h"
#include "mpu_types.h"
#include "riscv_decode.h"
#include "pmp.h"
#include "intc.h"
#include "device_access.h"
#include "athrill_idle_spin_halt.h"
#include <stddef.h>
#include <string.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

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

/*
 * The run's end state on stderr, for comparing runs exactly: the
 * instructions-retired counter (minstret, which the guest may also write)
 * and the registers that tell where the hart stopped.
 */
static void print_final_state(void)
{
	const TargetCoreType *core = &virtual_cpu.cores[0].core;

	fprintf(stderr, "FINAL minstret=%llu pc=0x%016llx ra=0x%016llx sp=0x%016llx priv=%u halt=%d"
		" mstatus=0x%016llx mcause=0x%016llx\n",
		(unsigned long long)core->internal_instret, (unsigned long long)core->reg.pc,
		(unsigned long long)core->reg.x[1], (unsigned long long)core->reg.x[CPU_REG_SP],
		(unsigned)core->reg.privilege, (int)core->is_halt,
		(unsigned long long)core->reg.csr.mstatus, (unsigned long long)core->reg.csr.mcause);
}

void cpu_init(void)
{
	static std_bool final_registered = FALSE;
	CoreIdType i;
	if (!final_registered) {
		(void)atexit(print_final_state);
		final_registered = TRUE;
	}
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

#ifdef ATHRILL_ENABLE_JIT
/*
 * JIT of the straight-line blocks (experiment, DEVICE_CONFIG_RISCV_JIT 1).
 *
 * jit_execute_block() gives the same clocks, counters and results as
 * execute_block(): the same block (build_block()), the same checks after
 * each instruction, the same counter updates. Only how the instructions run
 * differs: the integer add/sub/mul/div family and the immediate loads are
 * native host code; every other instruction calls its execute function from
 * the block (call-threaded). Rules kept by the translation:
 *   - (*clock) and pc are up to date before every call and at every exit;
 *     a native instruction only counts, its pc and clock are written later;
 *   - after a called instruction that is not the last one, the block ends
 *     when the pc is off the next entry, on an access fault or when a device
 *     was accessed (as execute_block() does);
 *   - the idle-function check runs before the instruction at the idle pc
 *     (idle_ready() is FALSE at any other pc);
 *   - x0 stays 0: a native instruction with rd 0 does nothing, a called one
 *     is followed by x[0] = 0.
 */
#include "athrill_jit.h"
#include "cpu_config.h"
#include "op_exec_memory.h"

typedef struct {
	uint64 trapped;
	uint64 *clock;
	uint32 word;	/* the 32-bit result of a W instruction */
	uint32 pending;	/* clocks to add before the next call */
	CoreIdType id;
	/*
	 * Native loads and stores (riscv_jit_emit_memory()): the core's memory
	 * window when the block starts (riscv_jit_window()). An access of
	 * 2^k bytes at addr is direct when (addr - lo) < read_limit[k] (or
	 * write_limit[k]); a limit of 0 sends every access to the C path.
	 * A store whose (addr - code_lo) < code_span may overwrite decoded
	 * code: it goes to the C path too, which invalidates the entries.
	 */
	uint64 addr;
	uint64 offset;
	uint64 bias;	/* host address of the window minus its guest start */
	uint64 lo;
	uint64 read_limit[4];
	uint64 write_limit[4];
	uint64 code_lo;
	uint64 code_span;
	/*
	 * Chaining: the clocks this run may use (a block's entry check exits
	 * when the blocks so far and itself would go over), and the slot of the
	 * unlinked exit the run ended at (0: none).
	 */
	uint64 budget;
	uint64 chain_slot;
} RiscvJitCtx;

static int riscv_jit_mode = -1;	/* -1: not read yet, 0: off, 1: on */
static AthrillJitCache *riscv_jit_cache;
static uint64 *riscv_jit_clock;
static uint64 riscv_jit_blocks_run;
static uint64 riscv_jit_insns_native;
static uint64 riscv_jit_insns_called;
static uint64 riscv_jit_translate_failures;
static uint64 riscv_jit_epoch = 1U;	/* changes when the cache flushes (entries' jit_code invalid) */
static uint32 riscv_jit_min_length = 2U;
/* measurements: instructions per block run (0..64), instructions run by blocks */
static uint64 riscv_jit_run_length[RISCV_BLOCK_MAX_LENGTH + 1U];
static uint64 riscv_jit_insns_run;
static uint64 riscv_jit_block_length_sum;
static std_bool riscv_jit_dumped;
static AthrillJitChainSlot *riscv_jit_pending_slot;	/* the last run's unlinked exit */
static uint64 riscv_jit_link_pmp_epoch;
static RiscvPrivilege riscv_jit_link_privilege = RISCV_PRIV_M;

/* Decoded code changed or the mode the links were made in: no link stays. */
void riscv_jit_code_changed(void)
{
	athrill_jit_chain_unlink_all();
	riscv_jit_pending_slot = NULL;
}

#define RV_X(n) athrill_jit_core((uint32)(offsetof(TargetCoreType, reg.x) + 8U * (uint32)(n)), 8)
#define RV_X32(n) athrill_jit_core((uint32)(offsetof(TargetCoreType, reg.x) + 8U * (uint32)(n)), 4)
#define RV_PC athrill_jit_core((uint32)offsetof(TargetCoreType, reg.pc), 8)
#define RV_WORD athrill_jit_ctx((uint32)offsetof(RiscvJitCtx, word), 4)
#define RV_PENDING athrill_jit_ctx((uint32)offsetof(RiscvJitCtx, pending), 4)
#define RV_CTX64(field) athrill_jit_ctx((uint32)offsetof(RiscvJitCtx, field), 8)

static void riscv_jit_report(void)
{
	AthrillJitCacheStats st;

	athrill_jit_cache_get_stats(riscv_jit_cache, &st);
	fprintf(stderr, "INFO: JIT blocks run=%" PRIu64 " compiled=%" PRIu64 " held=%u code=%" PRIu64
		" bytes stale=%" PRIu64 " flushes=%" PRIu64 " failures=%" PRIu64
		" translated insns native=%" PRIu64 " called=%" PRIu64 "\n",
		riscv_jit_blocks_run, st.inserts, st.blocks, st.code_bytes, st.stale, st.flushes,
		riscv_jit_translate_failures, riscv_jit_insns_native, riscv_jit_insns_called);
	fprintf(stderr, "INFO: JIT chain links held=%u\n", athrill_jit_chain_links());
	{
		uint32 n;
		uint64 cumulative = 0U;

		fprintf(stderr, "INFO: JIT insns run by blocks=%" PRIu64 " (%.1f per block run); built block length avg %.1f\n",
			riscv_jit_insns_run,
			(riscv_jit_blocks_run != 0U) ? (double)riscv_jit_insns_run / (double)riscv_jit_blocks_run : 0.0,
			(riscv_jit_blocks_run != 0U) ? (double)riscv_jit_block_length_sum / (double)riscv_jit_blocks_run : 0.0);
		fprintf(stderr, "INFO: JIT run length histogram (insns: block runs, cumulative %%):");
		for (n = 0U; n <= RISCV_BLOCK_MAX_LENGTH; n++) {
			if (riscv_jit_run_length[n] == 0U) {
				continue;
			}
			cumulative += riscv_jit_run_length[n];
			fprintf(stderr, " %u:%" PRIu64 "(%.0f%%)", n, riscv_jit_run_length[n],
				100.0 * (double)cumulative / (double)riscv_jit_blocks_run);
		}
		fprintf(stderr, "\n");
	}
}

static std_bool riscv_jit_enabled(void)
{
	uint32 value = 0U;

	if (riscv_jit_mode >= 0) {
		return riscv_jit_mode == 1;
	}
	riscv_jit_mode = 0;
	if ((cpuemu_get_devcfg_value("DEVICE_CONFIG_RISCV_JIT", &value) == STD_E_OK) && (value == 1U)) {
		riscv_jit_cache = athrill_jit_cache_create(65536U);
		if (riscv_jit_cache != NULL) {
			riscv_jit_mode = 1;
			if (cpuemu_get_devcfg_value("DEVICE_CONFIG_RISCV_JIT_MIN_LENGTH", &value) == STD_E_OK) {
				riscv_jit_min_length = value;
			}
			fprintf(stderr, "INFO: RISC-V JIT on (experiment), blocks of %u or more instructions\n",
				riscv_jit_min_length);
			(void)atexit(riscv_jit_report);
		}
	}
	return riscv_jit_mode == 1;
}

/*
 * A called instruction: brings clock and pc up to date, runs it as
 * execute_block() does, and returns 1 when the block must end after it
 * (pc off the next entry, an access fault, a device access).
 */
static int riscv_jit_call_insn(void *core_v, void *ctx_v, const void *arg)
{
	TargetCoreType *core = (TargetCoreType *)core_v;
	RiscvJitCtx *ctx = (RiscvJitCtx *)ctx_v;
	const RiscvDecodedInsn *entry = (const RiscvDecodedInsn *)arg;
	int retired;

	*ctx->clock += ctx->pending;
	core->reg.pc = entry->pc;
	retired = entry->execute(core, entry);
	if (core->reg.pc == entry->pc) {
		athrill_idle_spin_halt_after_execute(ctx->id, entry->pc);
	}
	if (retired != RISCV_EXEC_RETIRED) {
		ctx->trapped++;
	}
	core->reg.x[0] = 0;
	if (core->reg.pc != entry->pc + entry->length) {
		return 1;
	}
	if (core->access_fault || riscv_device_accessed) {
		return 1;
	}
	return 0;
}

/* Before the instruction at the idle pc: clock and pc up to date, then the check. */
static int riscv_jit_call_idle_ready(void *core_v, void *ctx_v, const void *arg)
{
	TargetCoreType *core = (TargetCoreType *)core_v;
	RiscvJitCtx *ctx = (RiscvJitCtx *)ctx_v;
	const RiscvDecodedInsn *entry = (const RiscvDecodedInsn *)arg;

	*ctx->clock += ctx->pending;
	core->reg.pc = entry->pc;
	return athrill_idle_spin_halt_idle_ready(ctx->id) ? 1 : 0;
}

/* RISC-V M division rules: x / 0 = all ones, x % 0 = x, MIN / -1 = MIN, MIN % -1 = 0. */
static void riscv_jit_emit_division(AthrillJitBuilder *b, AthrillJitAluOp op, const RiscvDecodedInsn *e)
{
	AthrillJitLabel *by_zero = athrill_jit_label_new(b);
	AthrillJitLabel *overflow = athrill_jit_label_new(b);
	AthrillJitLabel *end = athrill_jit_label_new(b);
	std_bool is_signed = (op == ATHRILL_JIT_DIVS) || (op == ATHRILL_JIT_REMS);
	std_bool is_div = (op == ATHRILL_JIT_DIVU) || (op == ATHRILL_JIT_DIVS);

	athrill_jit_jump_if(b, ATHRILL_JIT_EQ, RV_X(e->rs2), athrill_jit_imm(0), by_zero);
	if (is_signed) {
		AthrillJitLabel *normal = athrill_jit_label_new(b);

		athrill_jit_jump_if(b, ATHRILL_JIT_NE, RV_X(e->rs2), athrill_jit_imm(-1), normal);
		athrill_jit_jump_if(b, ATHRILL_JIT_EQ, RV_X(e->rs1), athrill_jit_imm((sint64)0x8000000000000000ULL), overflow);
		athrill_jit_label_place(b, normal);
	}
	athrill_jit_alu(b, op, RV_X(e->rd), RV_X(e->rs1), RV_X(e->rs2));
	athrill_jit_jump(b, end);

	athrill_jit_label_place(b, by_zero);
	if (is_div) {
		athrill_jit_mov(b, RV_X(e->rd), athrill_jit_imm(-1));
	} else {
		athrill_jit_mov(b, RV_X(e->rd), RV_X(e->rs1));
	}
	athrill_jit_jump(b, end);

	athrill_jit_label_place(b, overflow);
	if (is_div) {
		athrill_jit_mov(b, RV_X(e->rd), RV_X(e->rs1));	/* MIN */
	} else {
		athrill_jit_mov(b, RV_X(e->rd), athrill_jit_imm(0));
	}
	athrill_jit_label_place(b, end);
}

/*
 * The logic and shift instructions (op_exec_logic.c, op_exec_shift.c).
 * b == NULL: only says whether e is one of them. Shift counts: the host
 * masks them by (width - 1), the same as RISC-V (6 bits; 5 bits for W).
 * W forms work on the low 32 bits and sign-extend the result.
 */
static std_bool riscv_jit_emit_logic_shift(AthrillJitBuilder *b, const RiscvDecodedInsn *e)
{
	RiscvDecodedExecutor x = e->execute;
	sint64 imm = (sint64)e->immediate;
	AthrillJitAluOp op;
	std_bool word = FALSE;
	std_bool use_imm = TRUE;

	if (x == op_exec_andi) {
		op = ATHRILL_JIT_AND;
	} else if (x == op_exec_ori) {
		op = ATHRILL_JIT_OR;
	} else if (x == op_exec_xori) {
		op = ATHRILL_JIT_XOR;
	} else if (x == op_exec_and) {
		op = ATHRILL_JIT_AND;
		use_imm = FALSE;
	} else if (x == op_exec_or) {
		op = ATHRILL_JIT_OR;
		use_imm = FALSE;
	} else if (x == op_exec_xor) {
		op = ATHRILL_JIT_XOR;
		use_imm = FALSE;
	} else if (x == op_exec_slli) {
		op = ATHRILL_JIT_SHL;
		imm &= 63;
	} else if (x == op_exec_srli) {
		op = ATHRILL_JIT_SHR;
		imm &= 63;
	} else if (x == op_exec_srai) {
		op = ATHRILL_JIT_SAR;
		imm &= 63;
	} else if (x == op_exec_sll) {
		op = ATHRILL_JIT_SHL;
		use_imm = FALSE;
	} else if (x == op_exec_srl) {
		op = ATHRILL_JIT_SHR;
		use_imm = FALSE;
	} else if (x == op_exec_sra) {
		op = ATHRILL_JIT_SAR;
		use_imm = FALSE;
	} else if (x == op_exec_slliw) {
		op = ATHRILL_JIT_SHL;
		imm &= 31;
		word = TRUE;
	} else if (x == op_exec_srliw) {
		op = ATHRILL_JIT_SHR;
		imm &= 31;
		word = TRUE;
	} else if (x == op_exec_sraiw) {
		op = ATHRILL_JIT_SAR;
		imm &= 31;
		word = TRUE;
	} else if (x == op_exec_sllw) {
		op = ATHRILL_JIT_SHL;
		use_imm = FALSE;
		word = TRUE;
	} else if (x == op_exec_srlw) {
		op = ATHRILL_JIT_SHR;
		use_imm = FALSE;
		word = TRUE;
	} else if (x == op_exec_sraw) {
		op = ATHRILL_JIT_SAR;
		use_imm = FALSE;
		word = TRUE;
	} else {
		return FALSE;
	}
	if (b == NULL) {
		return TRUE;
	}
	if (word) {
		athrill_jit_alu(b, op, RV_WORD, RV_X32(e->rs1), use_imm ? athrill_jit_imm(imm) : RV_X32(e->rs2));
		athrill_jit_movs(b, RV_X(e->rd), RV_WORD);
	} else {
		athrill_jit_alu(b, op, RV_X(e->rd), RV_X(e->rs1), use_imm ? athrill_jit_imm(imm) : RV_X(e->rs2));
	}
	return TRUE;
}

/* Emits e natively; FALSE: not one of the native instructions. */
static std_bool riscv_jit_emit_native(AthrillJitBuilder *b, const RiscvDecodedInsn *e)
{
	RiscvDecodedExecutor x = e->execute;
	sint64 imm = (sint64)e->immediate;

	if ((x != op_exec_addi) && (x != op_exec_add) && (x != op_exec_sub) && (x != op_exec_mul)
	    && (x != op_exec_div) && (x != op_exec_divu) && (x != op_exec_rem) && (x != op_exec_remu)
	    && (x != op_exec_addiw) && (x != op_exec_addw) && (x != op_exec_subw)
	    && (x != op_exec_li) && (x != op_exec_lui) && (x != op_exec_auipc)
	    && (x != op_exec_mv) && (x != op_exec_discard)
	    && !riscv_jit_emit_logic_shift(NULL, e)) {
		return FALSE;
	}
	if ((e->rd == 0U) || (x == op_exec_discard)) {
		return TRUE;	/* a write to x0: nothing happens */
	}
	if (riscv_jit_emit_logic_shift(b, e)) {
		/* emitted */
	} else if (x == op_exec_mv) {
		athrill_jit_mov(b, RV_X(e->rd), RV_X(e->rs1));
	} else if (x == op_exec_auipc) {
		/* the pc is known when the block is translated */
		athrill_jit_mov(b, RV_X(e->rd), athrill_jit_imm((sint64)(e->pc + e->immediate)));
	} else if (x == op_exec_addi) {
		athrill_jit_alu(b, ATHRILL_JIT_ADD, RV_X(e->rd), RV_X(e->rs1), athrill_jit_imm(imm));
	} else if (x == op_exec_add) {
		athrill_jit_alu(b, ATHRILL_JIT_ADD, RV_X(e->rd), RV_X(e->rs1), RV_X(e->rs2));
	} else if (x == op_exec_sub) {
		athrill_jit_alu(b, ATHRILL_JIT_SUB, RV_X(e->rd), RV_X(e->rs1), RV_X(e->rs2));
	} else if (x == op_exec_mul) {
		athrill_jit_alu(b, ATHRILL_JIT_MUL, RV_X(e->rd), RV_X(e->rs1), RV_X(e->rs2));
	} else if (x == op_exec_div) {
		riscv_jit_emit_division(b, ATHRILL_JIT_DIVS, e);
	} else if (x == op_exec_divu) {
		riscv_jit_emit_division(b, ATHRILL_JIT_DIVU, e);
	} else if (x == op_exec_rem) {
		riscv_jit_emit_division(b, ATHRILL_JIT_REMS, e);
	} else if (x == op_exec_remu) {
		riscv_jit_emit_division(b, ATHRILL_JIT_REMU, e);
	} else if (x == op_exec_addiw) {
		/* RV64 W: the 32-bit result, sign-extended */
		athrill_jit_alu(b, ATHRILL_JIT_ADD, RV_WORD, RV_X32(e->rs1), athrill_jit_imm(imm));
		athrill_jit_movs(b, RV_X(e->rd), RV_WORD);
	} else if (x == op_exec_addw) {
		athrill_jit_alu(b, ATHRILL_JIT_ADD, RV_WORD, RV_X32(e->rs1), RV_X32(e->rs2));
		athrill_jit_movs(b, RV_X(e->rd), RV_WORD);
	} else if (x == op_exec_subw) {
		athrill_jit_alu(b, ATHRILL_JIT_SUB, RV_WORD, RV_X32(e->rs1), RV_X32(e->rs2));
		athrill_jit_movs(b, RV_X(e->rd), RV_WORD);
	} else {
		/* li, lui: the decoded immediate */
		athrill_jit_mov(b, RV_X(e->rd), athrill_jit_imm(imm));
	}
	return TRUE;
}

/* The integer loads and stores: size, sign extension, store. FALSE: not one. */
static std_bool riscv_jit_memory_kind(RiscvDecodedExecutor x, uint8 *size, std_bool *sign_extend, std_bool *store)
{
	*sign_extend = FALSE;
	*store = FALSE;
	if (x == op_exec_lb) {
		*size = 1U;
		*sign_extend = TRUE;
	} else if (x == op_exec_lh) {
		*size = 2U;
		*sign_extend = TRUE;
	} else if (x == op_exec_lw) {
		*size = 4U;
		*sign_extend = TRUE;
	} else if (x == op_exec_ld) {
		*size = 8U;
	} else if (x == op_exec_lbu) {
		*size = 1U;
	} else if (x == op_exec_lhu) {
		*size = 2U;
	} else if (x == op_exec_lwu) {
		*size = 4U;
	} else if (x == op_exec_sb) {
		*size = 1U;
		*store = TRUE;
	} else if (x == op_exec_sh) {
		*size = 2U;
		*store = TRUE;
	} else if (x == op_exec_sw) {
		*size = 4U;
		*store = TRUE;
	} else if (x == op_exec_sd) {
		*size = 8U;
		*store = TRUE;
	} else {
		return FALSE;
	}
	return TRUE;
}

static uint32 riscv_jit_size_index(uint8 size)
{
	if (size == 1U) {
		return 0U;
	}
	if (size == 2U) {
		return 1U;
	}
	if (size == 4U) {
		return 2U;
	}
	return 3U;
}

/*
 * A load or store with a direct path and the C path:
 *
 *   t0 = addr = x[rs1] + imm
 *   if (addr - lo) >= limit -> C path          (outside the window, or PMP)
 *   store: if (addr - code_lo) < code_span -> C path   (may hit decoded code)
 *   direct: x[rd] = host[addr] / host[addr] = x[rs2]
 *
 * The direct path is what op_exec_memory_direct() + op_exec_memory_read()
 * / _write() do for an access inside the window that PMP allows (Erbium
 * mode: any alignment), with no fault and no device access, so the block
 * goes on as execute_block() does. The C path is a called instruction,
 * placed after the block's end (riscv_jit_emit_c_paths()) so that the
 * direct paths run straight through.
 * A load to x0 (no effect) and an Erbium-incompatible build stay called.
 */
typedef struct {
	AthrillJitLabel *start;
	AthrillJitLabel *resume;
	const RiscvDecodedInsn *entry;
	uint32 pending;
	std_bool last;
	std_bool taken;	/* a taken branch's exit (riscv_jit_emit_branch()), not a call */
	uint64 target;
} RiscvJitCPath;

#define RV_T0 athrill_jit_temp(0U, 8)
#define RV_T1 athrill_jit_temp(1U, 8)

static std_bool riscv_jit_emit_memory(AthrillJitBuilder *b, const RiscvDecodedInsn *e, uint32 pending, std_bool last,
				      RiscvJitCPath *c_path)
{
	uint8 size;
	std_bool sign_extend;
	std_bool store;
	uint32 k;

	if (!CPU_CONFIG_ERBIUM_COMPAT) {
		return FALSE;
	}
	if (!riscv_jit_memory_kind(e->execute, &size, &sign_extend, &store)) {
		return FALSE;
	}
	if (!store && (e->rd == 0U)) {
		return FALSE;
	}
	k = riscv_jit_size_index(size);
	c_path->start = athrill_jit_label_new(b);
	c_path->resume = athrill_jit_label_new(b);
	c_path->entry = e;
	c_path->pending = pending;
	c_path->last = last;
	c_path->taken = FALSE;
	c_path->target = 0U;

	athrill_jit_alu(b, ATHRILL_JIT_ADD, RV_T0, RV_X(e->rs1), athrill_jit_imm((sint64)e->immediate));
	athrill_jit_alu(b, ATHRILL_JIT_SUB, RV_T1, RV_T0, RV_CTX64(lo));
	if (store) {
		athrill_jit_jump_if(b, ATHRILL_JIT_GEU, RV_T1, RV_CTX64(write_limit[k]), c_path->start);
		athrill_jit_alu(b, ATHRILL_JIT_SUB, RV_T1, RV_T0, RV_CTX64(code_lo));
		athrill_jit_jump_if(b, ATHRILL_JIT_LTU, RV_T1, RV_CTX64(code_span), c_path->start);
		athrill_jit_store_host(b, RV_X(e->rs2), RV_T0, RV_CTX64(bias), size);
	} else {
		athrill_jit_jump_if(b, ATHRILL_JIT_GEU, RV_T1, RV_CTX64(read_limit[k]), c_path->start);
		athrill_jit_load_host(b, RV_X(e->rd), RV_T0, RV_CTX64(bias), size, sign_extend);
	}
	athrill_jit_label_place(b, c_path->resume);
	return TRUE;
}

/*
 * An exit to a known pc, with pc and clock written: a chain exit, so that
 * the block that starts at pc can follow without a return to C
 * (jit_execute_block() links it). The idle pc is never chained to: the
 * idle-function check runs before it, in C.
 */
static void riscv_jit_exit_to(AthrillJitBuilder *b, uint64 pc)
{
	AthrillAddrType idle_pc = athrill_idle_spin_halt_idle_pc();

	if ((idle_pc != 0U) && (pc == idle_pc)) {
		athrill_jit_exit(b);
		return;
	}
	athrill_jit_chain_exit(b, RV_CTX64(chain_slot), pc);
}

/*
 * The C paths, after the block's end. Clocks: the call adds the pending
 * clocks; when the block goes on, they are taken off again, as the direct
 * path has not added them yet (pending keeps counting for both paths).
 * The last instruction's C path ends the block: its pc and clock are the
 * instruction's.
 */
static void riscv_jit_emit_c_paths(AthrillJitBuilder *b, const RiscvJitCPath *c_paths, uint32 count)
{
	uint32 i;

	for (i = 0U; i < count; i++) {
		athrill_jit_label_place(b, c_paths[i].start);
		if (c_paths[i].taken) {
			/* a taken branch: the block ends at the target */
			athrill_jit_alu(b, ATHRILL_JIT_ADD, athrill_jit_abs(riscv_jit_clock, 8),
					athrill_jit_abs(riscv_jit_clock, 8), athrill_jit_imm(c_paths[i].pending));
			athrill_jit_mov(b, RV_PC, athrill_jit_imm((sint64)c_paths[i].target));
			riscv_jit_exit_to(b, c_paths[i].target);
			continue;
		}
		athrill_jit_mov(b, RV_PENDING, athrill_jit_imm(c_paths[i].pending));
		athrill_jit_call(b, riscv_jit_call_insn, c_paths[i].entry);
		if (c_paths[i].last) {
			athrill_jit_exit(b);
			continue;
		}
		athrill_jit_exit_if(b, ATHRILL_JIT_NE, athrill_jit_result(), athrill_jit_imm(0));
		athrill_jit_alu(b, ATHRILL_JIT_SUB, athrill_jit_abs(riscv_jit_clock, 8), athrill_jit_abs(riscv_jit_clock, 8),
				athrill_jit_imm(c_paths[i].pending));
		athrill_jit_jump(b, c_paths[i].resume);
	}
}

/*
 * Conditional branches (op_exec_branch.c). Not taken, the block goes on;
 * taken, it ends with the pc at the target (as execute_block() ends when
 * the pc is off the next entry), through an exit placed after the block's
 * end. The target is known at translation. Stay called: a misaligned
 * target (it traps) and a branch to itself (the idle and spin-halt checks
 * after the instruction need the C path).
 */
static std_bool riscv_jit_emit_branch(AthrillJitBuilder *b, const RiscvDecodedInsn *e, uint32 pending,
				      RiscvJitCPath *taken)
{
	RiscvDecodedExecutor x = e->execute;
	uint64 target = e->pc + e->immediate;
	AthrillJitCond cond;

	if (x == op_exec_beq) {
		cond = ATHRILL_JIT_EQ;
	} else if (x == op_exec_bne) {
		cond = ATHRILL_JIT_NE;
	} else if (x == op_exec_blt) {
		cond = ATHRILL_JIT_LTS;
	} else if (x == op_exec_bge) {
		cond = ATHRILL_JIT_GES;
	} else if (x == op_exec_bltu) {
		cond = ATHRILL_JIT_LTU;
	} else if (x == op_exec_bgeu) {
		cond = ATHRILL_JIT_GEU;
	} else {
		return FALSE;
	}
	if (((target & 1U) != 0U) || (target == e->pc)) {
		return FALSE;
	}
	taken->start = athrill_jit_label_new(b);
	taken->resume = NULL;
	taken->entry = e;
	taken->pending = pending;
	taken->last = FALSE;
	taken->taken = TRUE;
	taken->target = target;
	athrill_jit_jump_if(b, cond, RV_X(e->rs1), RV_X(e->rs2), taken->start);
	return TRUE;
}

/*
 * jal and jalr, the last instruction of a block: x[rd] = the next pc, the
 * pc = the target. *pc_written: the epilogue must not set the pc again.
 * jal: the target is known; a misaligned one (it traps) or a jump to itself
 * (idle and spin-halt checks) stays called. jalr: the target is cleared of
 * bit 0, so it cannot be misaligned; a jump to itself goes to the C path,
 * which runs the whole instruction again (nothing is written before it).
 */
static std_bool riscv_jit_emit_jump(AthrillJitBuilder *b, const RiscvDecodedInsn *e, uint32 pending,
				    RiscvJitCPath *c_path, std_bool *pc_written, std_bool *known, uint64 *next_pc)
{
	uint64 next = e->pc + e->length;
	uint64 target;

	if (e->execute == op_exec_jal) {
		target = e->pc + e->immediate;
		if (((target & 1U) != 0U) || (target == e->pc)) {
			return FALSE;
		}
		if (e->rd != 0U) {
			athrill_jit_mov(b, RV_X(e->rd), athrill_jit_imm((sint64)next));
		}
		athrill_jit_mov(b, RV_PC, athrill_jit_imm((sint64)target));
		*pc_written = TRUE;
		*next_pc = target;
		return TRUE;
	}
	if (e->execute != op_exec_jalr) {
		return FALSE;
	}
	c_path->start = athrill_jit_label_new(b);
	c_path->resume = NULL;
	c_path->entry = e;
	c_path->pending = pending;
	c_path->last = TRUE;
	c_path->taken = FALSE;
	c_path->target = 0U;
	athrill_jit_alu(b, ATHRILL_JIT_ADD, RV_T0, RV_X(e->rs1), athrill_jit_imm((sint64)e->immediate));
	athrill_jit_alu(b, ATHRILL_JIT_AND, RV_T0, RV_T0, athrill_jit_imm(-2));
	athrill_jit_jump_if(b, ATHRILL_JIT_EQ, RV_T0, athrill_jit_imm((sint64)e->pc), c_path->start);
	if (e->rd != 0U) {
		athrill_jit_mov(b, RV_X(e->rd), athrill_jit_imm((sint64)next));
	}
	athrill_jit_mov(b, RV_PC, RV_T0);
	*pc_written = TRUE;
	*known = FALSE;	/* the target is only known at run time */
	return TRUE;
}

/*
 * The memory window for the direct paths of one block run. The window
 * may change during the block (a C-path access elsewhere refills it), but
 * the region recorded here stays where it is (regions do not move), and
 * PMP and the privilege only change at the last instruction of a block.
 */
typedef struct {
	const TargetCoreType *core;
	std_bool valid;
	AthrillAddrType start;
	uint8 *data;
	uint64 pmp_epoch;
	RiscvPrivilege privilege;
	uint64 decode_lo;
	uint64 decode_hi;
} RiscvJitWindowKey;

static RiscvJitWindowKey riscv_jit_window_key;
static std_bool riscv_jit_window_known;

static inline std_bool riscv_jit_window_unchanged(const TargetCoreType *core)
{
	const RiscvJitWindowKey *key = &riscv_jit_window_key;

	return riscv_jit_window_known && (key->core == core) && (key->valid == core->memory_window.valid)
	       && (key->start == core->memory_window.start) && (key->data == core->memory_window.data)
	       && (key->pmp_epoch == core->pmp_epoch) && (key->privilege == core->reg.privilege)
	       && (key->decode_lo == core->decode_lo) && (key->decode_hi == core->decode_hi);
}

/* Works out ctx's window fields again (they stay in ctx between runs). */
static void riscv_jit_window(TargetCoreType *core, RiscvJitCtx *ctx)
{
	RiscvMemoryWindow *window = &core->memory_window;
	uint64 size;
	uint64 width;
	uint32 k;

	for (k = 0U; k < 4U; k++) {
		ctx->read_limit[k] = 0U;
		ctx->write_limit[k] = 0U;
	}
	ctx->lo = 0U;
	ctx->bias = 0U;
	if (window->valid) {
		/* brings the window's PMP answers up to date (as the next access would) */
		(void)op_exec_memory_direct(core, window->start, 1U, CpuMemoryAccess_READ);
		size = window->end - window->start;
		ctx->lo = window->start;
		ctx->bias = (uint64)(uintptr_t)window->data - window->start;
		for (k = 0U; k < 4U; k++) {
			width = 1ULL << k;
			if (size < width) {
				continue;
			}
			if (window->pmp_read) {
				ctx->read_limit[k] = size - width + 1U;
			}
			if (window->pmp_write) {
				ctx->write_limit[k] = size - width + 1U;
			}
		}
	}
	/*
	 * A store of up to 8 bytes at addr overwrites instructions starting in
	 * [addr - 3, addr + 7]: it may hit decoded code when that meets
	 * [decode_lo, decode_hi). No decode page: no store can.
	 */
	if (core->decode_hi == 0U) {
		ctx->code_lo = 0U;
		ctx->code_span = 0U;
	} else {
		ctx->code_lo = (core->decode_lo >= 7U) ? core->decode_lo - 7U : 0U;
		ctx->code_span = core->decode_hi + 3U - ctx->code_lo;
	}
	riscv_jit_window_key.core = core;
	riscv_jit_window_key.valid = window->valid;
	riscv_jit_window_key.start = window->start;
	riscv_jit_window_key.data = window->data;
	riscv_jit_window_key.pmp_epoch = core->pmp_epoch;
	riscv_jit_window_key.privilege = core->reg.privilege;
	riscv_jit_window_key.decode_lo = core->decode_lo;
	riscv_jit_window_key.decode_hi = core->decode_hi;
	riscv_jit_window_known = TRUE;
}

static void riscv_jit_flush_clock(AthrillJitBuilder *b, uint32 *pending)
{
	if (*pending == 0U) {
		return;
	}
	athrill_jit_alu(b, ATHRILL_JIT_ADD, athrill_jit_abs(riscv_jit_clock, 8), athrill_jit_abs(riscv_jit_clock, 8),
			athrill_jit_imm(*pending));
	*pending = 0U;
}

static AthrillJitCode *riscv_jit_translate(const RiscvDecodedInsn *first, uint64 length)
{
	AthrillJitBuilder *b = athrill_jit_builder_create();
	const RiscvDecodedInsn *e = first;
	AthrillAddrType idle_pc = athrill_idle_spin_halt_idle_pc();
	uint32 pending = 0U;
	std_bool last_native = FALSE;
	RiscvJitCPath c_paths[RISCV_BLOCK_MAX_LENGTH];
	uint32 c_path_count = 0U;
	std_bool pc_written = FALSE;
	std_bool next_known = TRUE;
	uint64 next_pc = 0U;
	uint64 i;

	if (b == NULL) {
		return NULL;
	}
	/* entry check: a chained run stops before a block that would go over its budget */
	athrill_jit_alu(b, ATHRILL_JIT_ADD, RV_T0, athrill_jit_done(), athrill_jit_imm((sint64)length));
	athrill_jit_exit_if(b, ATHRILL_JIT_GTU, RV_T0, RV_CTX64(budget));
	for (i = 0U; i < length; i++) {
		std_bool last = (i + 1U == length);

		if ((idle_pc != 0U) && (e->pc == idle_pc)) {
			athrill_jit_mov(b, RV_PENDING, athrill_jit_imm(pending));
			pending = 0U;
			athrill_jit_call(b, riscv_jit_call_idle_ready, e);
			athrill_jit_exit_if(b, ATHRILL_JIT_NE, athrill_jit_result(), athrill_jit_imm(0));
		}
		athrill_jit_count(b, 1U);
		pending++;
		if (riscv_jit_emit_memory(b, e, pending, last, &c_paths[c_path_count])) {
			c_path_count++;
			riscv_jit_insns_native++;
			last_native = TRUE;
		} else if (riscv_jit_emit_branch(b, e, pending, &c_paths[c_path_count])) {
			c_path_count++;
			riscv_jit_insns_native++;
			last_native = TRUE;
		} else if (last && riscv_jit_emit_jump(b, e, pending, &c_paths[c_path_count], &pc_written, &next_known,
						     &next_pc)) {
			if (e->execute == op_exec_jalr) {
				c_path_count++;
			}
			riscv_jit_insns_native++;
			last_native = TRUE;
		} else if (riscv_jit_emit_native(b, e)) {
			riscv_jit_insns_native++;
			last_native = TRUE;
		} else {
			athrill_jit_mov(b, RV_PENDING, athrill_jit_imm(pending));
			pending = 0U;
			athrill_jit_call(b, riscv_jit_call_insn, e);
			if (!last) {
				athrill_jit_exit_if(b, ATHRILL_JIT_NE, athrill_jit_result(), athrill_jit_imm(0));
			}
			riscv_jit_insns_called++;
			last_native = FALSE;
		}
		if (!last) {
			e += e->length >> 1U;
		}
	}
	if (last_native) {
		riscv_jit_flush_clock(b, &pending);
		if (!pc_written) {
			next_pc = e->pc + e->length;
			athrill_jit_mov(b, RV_PC, athrill_jit_imm((sint64)next_pc));
		}
		if (next_known) {
			riscv_jit_exit_to(b, next_pc);
		}
	}
	if (c_path_count != 0U) {
		athrill_jit_exit(b);
		riscv_jit_emit_c_paths(b, c_paths, c_path_count);
	}
	return athrill_jit_builder_finish(b);
}

/* execute_block() with the block's code from the JIT (same arguments and result). */
static inline uint64 jit_execute_block(TargetCoreType *core, CoreIdType id, uint64 budget, uint64 *clock)
{
	uint64 pc = core->reg.pc;
	RiscvDecodePage *page;
	RiscvDecodedInsn *entry;
	uint64 length;
	uint64 generation;
	const AthrillJitCode *code;
	AthrillJitCode *fresh;
	static RiscvJitCtx ctx;	/* the window fields stay between runs */
	uint64 done;

	if (riscv_jit_clock == NULL) {
		riscv_jit_clock = clock;
	}
	if (clock != riscv_jit_clock) {
		return execute_block(core, id, budget, clock);
	}
	if ((core->pmp_epoch != riscv_jit_link_pmp_epoch) || (core->reg.privilege != riscv_jit_link_privilege)) {
		/* links skip the fetch checks made under the mode they were made in */
		riscv_jit_code_changed();
		riscv_jit_link_pmp_epoch = core->pmp_epoch;
		riscv_jit_link_privilege = core->reg.privilege;
	}
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
	if (length < riscv_jit_min_length) {
		return execute_block(core, id, budget, clock);
	}
	generation = ((uint64)page->generation << 8) | (uint64)entry->block_length;
	/*
	 * The entry holds its block's code. An entry without it (new, refilled,
	 * or its page re-created after being replaced in the decode directory)
	 * is translated again: the cache only owns the code, as a page that was
	 * away may come back with its generation restarted.
	 */
	code = NULL;
	if ((entry->jit_code != NULL) && (entry->jit_generation == generation) && (entry->jit_epoch == riscv_jit_epoch)) {
		code = (const AthrillJitCode *)entry->jit_code;
	}
	if (code == NULL) {
		fresh = riscv_jit_translate(entry, length);
		if (fresh == NULL) {
			riscv_jit_translate_failures++;
			return execute_block(core, id, budget, clock);
		}
		athrill_jit_cache_insert(riscv_jit_cache, pc, (uint32)core->reg.privilege, generation, fresh);
		riscv_jit_pending_slot = NULL;	/* the insert may have freed the block it belongs to */
		{
			AthrillJitCacheStats st;

			athrill_jit_cache_get_stats(riscv_jit_cache, &st);
			if (st.flushes + 1U != riscv_jit_epoch) {
				riscv_jit_epoch = st.flushes + 1U;	/* every entry's jit_code is gone */
			}
		}
		code = fresh;
		if (!riscv_jit_dumped && (length >= 8U) && (getenv("RISCV_JIT_DUMP") != NULL)) {
			/* one block's host code, for disassembly */
			FILE *f = fopen(getenv("RISCV_JIT_DUMP"), "wb");

			if (f != NULL) {
				(void)fwrite(athrill_jit_code_entry(code), 1U, athrill_jit_code_size(code), f);
				(void)fclose(f);
				fprintf(stderr, "INFO: JIT dumped block pc=0x%" PRIx64 " length=%" PRIu64 " bytes=%u\n",
					pc, length, athrill_jit_code_size(code));
			}
			riscv_jit_dumped = TRUE;
		}
	}
	entry->jit_code = code;
	entry->jit_generation = generation;
	entry->jit_epoch = riscv_jit_epoch;
	/* the run before ended at an unlinked exit to this pc: link it here */
	if ((riscv_jit_pending_slot != NULL) && (athrill_jit_chain_tag(riscv_jit_pending_slot) == pc)) {
		athrill_jit_chain_link(riscv_jit_pending_slot, code);
	}
	riscv_jit_pending_slot = NULL;
	core->access_fault = FALSE;
	ctx.trapped = 0U;
	ctx.clock = clock;
	ctx.word = 0U;
	ctx.pending = 0U;
	ctx.id = id;
	if (!riscv_jit_window_unchanged(core)) {
		riscv_jit_window(core, &ctx);
	}
	ctx.budget = budget;
	ctx.chain_slot = 0U;
	done = athrill_jit_code_run(code, core, &ctx);
	riscv_jit_pending_slot = (AthrillJitChainSlot *)(uintptr_t)ctx.chain_slot;
	riscv_jit_blocks_run++;
	riscv_jit_insns_run += done;
	riscv_jit_block_length_sum += length;
	riscv_jit_run_length[(done <= RISCV_BLOCK_MAX_LENGTH) ? done : RISCV_BLOCK_MAX_LENGTH]++;
	core->internal_cycle += done;
	core->internal_instret += done - ctx.trapped;
	virtual_cpu.cores[id].elaps += done;
	return done;
}
#endif /* ATHRILL_ENABLE_JIT */

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
#ifdef ATHRILL_ENABLE_JIT
			if (riscv_jit_enabled()) {
				done = jit_execute_block(core, core_id, max_clocks - n, clock);
			} else {
				done = execute_block(core, core_id, max_clocks - n, clock);
			}
#else
			done = execute_block(core, core_id, max_clocks - n, clock);
#endif
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
