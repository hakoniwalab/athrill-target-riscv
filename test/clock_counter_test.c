/*
 * CPU clock and instruction counters, traps, the decode cache after code
 * modification, misaligned accesses, Erbium counter CSRs, fence.i, wfi,
 * the timer and the PLIC, driven directly through cpu_supply_clock(), and
 * a block after a store into its code, through cpu_supply_clocks().
 *
 * Instruction words used (hand-assembled):
 *   0x00108093 addi x1, x1, 1      0x00000073 ecall
 *   0x00100073 ebreak              0x00100293 addi x5, x0, 1
 *   0x00200293 addi x5, x0, 2      0x0020a023 sw x2, 0(x1)
 *   0x00c0006f jal x0, +12         0x0020b023 sd x2, 0(x1)
 *   0x0000b183 ld x3, 0(x1)        0xb0002273 csrrs x4, mcycle, x0
 *   0x0000100f fence.i             0x10500073 wfi
 */
#include "cpu.h"
#include "std_device_ops.h"
#include "std_cpu_ops.h"
#include "athrill_idle_spin_halt.h"
#include <stdio.h>
#include <string.h>
#include "peripheral/erbium_devices.h"

#define HOST_MEMORY_SIZE 256U
#include "common/host_memory_stub.h"

#define INSN_ADDI_X1_1 0x00108093U
#define INSN_ECALL 0x00000073U
#define INSN_EBREAK 0x00100073U

static unsigned failures;
static uint32 spin_halt_option;

#ifdef EXDEV_ENABLE
/* No external devices in this test. */
void device_supply_clock_exdev(DeviceClockType *clock)
{
	(void)clock;
}

void device_init_athrill_exdev(void)
{
}
#endif

Std_ReturnType cpuemu_get_devcfg_value(const char *key, uint32 *value)
{
	if (strcmp(key, "DEVICE_CONFIG_SPIN_HALT") == 0) {
		*value = spin_halt_option;
		return STD_E_OK;
	}
	if (strcmp(key, "DEBUG_FUNC_ENABLE_SKIP_CLOCK") == 0) {
		*value = 1U;
		return STD_E_OK;
	}
	return STD_E_INVALID;
}

uint64 cpuemu_get_clock(void)
{
	return 0;
}

Std_ReturnType cpuemu_get_devcfg_string(const char *key, char **value)
{
	(void)key;
	(void)value;
	return STD_E_INVALID;
}

int symbol_get_func(char *name, uint32 length, AthrillAddrType *address, uint32 *size)
{
	(void)name;
	(void)length;
	(void)address;
	(void)size;
	return -1;
}

extern void device_init(CpuType *, DeviceClockType *);
extern void device_supply_clock(DeviceClockType *);

static void check(const char *name, int passed)
{
	printf("%s %s\n", passed ? "PASS" : "FAIL", name);
	if (!passed) {
		failures++;
	}
}

static void put_insn(unsigned address, uint32 instruction)
{
	memcpy(&memory[address], &instruction, 4);
}

static TargetCoreType *reset_core(void)
{
	cpu_reset(0);
	return &virtual_cpu.cores[0].core;
}

static void check_counters_and_traps(DeviceClockType *clock)
{
	TargetCoreType *core = &virtual_cpu.cores[0].core;
	unsigned n;

	for (n = 0; n < 10; n++) {
		put_insn(n * 4U, INSN_ADDI_X1_1);
	}
	put_insn(40, INSN_ECALL);
	put_insn(44, INSN_EBREAK);
	for (n = 0; n < 10; n++) {
		device_supply_clock(clock);
		cpu_supply_clock(0);
		clock->clock++;
	}
	check("10 clocks: device clock, cycles, retired, x1, elapsed all 10",
		clock->clock == 10U && core->internal_cycle == 10U && core->internal_instret == 10U
		&& core->reg.x[1] == 10U && virtual_cpu.cores[0].elaps == 10U);

	core->reg.csr.mtvec = 44;
	cpu_supply_clock(0);
	check("ecall: trap from Machine, counts a cycle but no retirement",
		core->internal_cycle == 11U && core->internal_instret == 10U
		&& core->reg.csr.mcause == 11U && core->reg.csr.mepc == 40U);
	cpu_supply_clock(0);
	check("ebreak at the trap vector: breakpoint, not retired",
		core->reg.csr.mcause == 3U && core->internal_instret == 10U);
}

static void check_modified_code(void)
{
	TargetCoreType *core = reset_core();

	/* Cache the instruction at 16, rewrite it with SW at 0, then run it. */
	put_insn(16, 0x00100293U);
	core->reg.pc = 16;
	cpu_supply_clock(0);
	put_insn(0, 0x0020a023U);
	put_insn(4, 0x00c0006fU);
	core->reg.x[1] = 16;
	core->reg.x[2] = 0x00200293U;
	core->reg.pc = 0;
	cpu_supply_clock(0);
	cpu_supply_clock(0);
	cpu_supply_clock(0);
	check("rewritten instruction runs in its new form", core->reg.x[5] == 2U);
}

/*
 * Batched execution runs straight-line blocks of cached instructions. A
 * store into a block's code, here its second instruction, must drop it:
 *   0: addi x6, x6, 0      32: sw x7, 4(x0)
 *   4: addi x5, x5, 1      36: jal x0, -36
 *   8: jal x0, -8
 */
static void check_block_after_store(void)
{
	TargetCoreType *core = reset_core();
	uint64 clock = 0;
	uint64 executed = 0;

	put_insn(0, 0x00030313U);
	put_insn(4, 0x00128293U);
	put_insn(8, 0xff9ff06fU);
	put_insn(32, 0x00702223U);
	put_insn(36, 0xfddff06fU);
	core->reg.x[7] = 0x01028293U;	/* addi x5, x5, 16 */
	riscv_device_accessed = FALSE;	/* left set by the device accesses before */
	(void)cpu_supply_clocks(0, 21, &clock, &executed);
	check("a batch of 21 clocks runs the loop 7 times",
		core->reg.x[5] == 7U && clock == 21U && executed == 21U && core->internal_cycle == 21U);
	core->reg.pc = 32;
	(void)cpu_supply_clocks(0, 8, &clock, &executed);
	check("after a store into the loop, its new instruction runs",
		core->reg.x[5] == 39U && clock == 29U && executed == 8U && core->reg.pc == 0U);
}

/*
 * A conditional branch inside a block: taken, it leaves the block; not
 * taken, the block goes on.
 *   0: addi x5, x5, 1      8: addi x7, x7, 1
 *   4: bne x5, x6, -4     12: jal x0, 0
 */
static void check_block_through_branch(void)
{
	TargetCoreType *core = reset_core();
	uint64 clock = 0;
	uint64 executed = 0;

	put_insn(0, 0x00128293U);
	put_insn(4, 0xfe629ee3U);
	put_insn(8, 0x00138393U);
	put_insn(12, 0x0000006fU);
	core->reg.x[6] = 5;
	riscv_device_accessed = FALSE;
	(void)cpu_supply_clocks(0, 11, &clock, &executed);
	check("a loop on bne runs 5 times, then the block goes on past it",
		core->reg.x[5] == 5U && core->reg.x[7] == 1U && core->reg.pc == 12U && clock == 11U &&
		core->internal_cycle == 11U && core->internal_instret == 11U);
}

/*
 * The cycle counter read by the CSR instruction that ends a block counts
 * the instructions before it in the block (standard build; Erbium reads 0).
 *   0: addi x1, x1, 1      8: csrrs x4, mcycle, x0
 *   4: addi x1, x1, 1     12: jal x0, 0
 */
static void check_block_counter_read(void)
{
	TargetCoreType *core = reset_core();
	uint64 clock = 0;
	uint64 executed = 0;

	put_insn(0, INSN_ADDI_X1_1);
	put_insn(4, INSN_ADDI_X1_1);
	put_insn(8, 0xb0002273U);
	put_insn(12, 0x0000006fU);
	riscv_device_accessed = FALSE;
	(void)cpu_supply_clocks(0, 4, &clock, &executed);	/* decodes the four instructions */
	core->reg.pc = 0;
	(void)cpu_supply_clocks(0, 3, &clock, &executed);	/* runs them as one block */
#if CPU_CONFIG_ERBIUM_COMPAT
	check("mcycle read at the end of a block is 0 (Erbium)",
		core->reg.x[4] == 0U && core->internal_cycle == 7U && core->internal_instret == 7U);
#else
	check("mcycle read at the end of a block counts the block's earlier instructions",
		core->reg.x[4] == 6U && core->internal_cycle == 7U && core->internal_instret == 7U);
#endif
}

static void check_misaligned_doubleword(void)
{
	TargetCoreType *core = reset_core();

	put_insn(0, 0x0020b023U);
	put_insn(4, 0x0000b183U);
	core->reg.x[1] = 101;
	core->reg.x[2] = 0x8877665544332211ULL;
	cpu_supply_clock(0);
#if CPU_CONFIG_ERBIUM_COMPAT
	cpu_supply_clock(0);
	check("misaligned sd/ld keep the whole value (Erbium)",
		core->reg.x[3] == 0x8877665544332211ULL);
#else
	check("misaligned sd: store address misaligned (standard)",
		core->reg.csr.mcause == 6U);
#endif
}

static void check_counter_csr(void)
{
	TargetCoreType *core = reset_core();

	put_insn(0, 0xb0002273U);
	cpu_supply_clock(0);
	/* As the first instruction mcycle is 0 in both modes; Erbium keeps it 0. */
	check("mcycle read as the first instruction is 0; the instruction is counted",
		core->reg.x[4] == 0U && core->internal_cycle == 1U && core->internal_instret == 1U);
}

static void check_fence_i(void)
{
	TargetCoreType *core = reset_core();

	put_insn(0, 0x0000100fU);
	core->reg.csr.mtvec = 32;
	cpu_supply_clock(0);
#if CPU_CONFIG_ERBIUM_COMPAT
	check("fence.i raises cause 30 (Erbium)",
		core->reg.csr.mcause == 30U && core->reg.csr.mtval == 0x0000100fU && core->reg.pc == 32U);
#else
	check("fence.i retires (standard)",
		core->reg.pc == 4U && core->internal_instret == 1U);
#endif
}

static void check_wfi_and_timer(DeviceClockType *clock)
{
	TargetCoreType *core = reset_core();
	uint32 value = 0;

	put_insn(0, 0x10500073U);
	core->reg.csr.mie = 1ULL << 3;
	cpu_supply_clock(0);
	check("wfi halts", core->is_halt);
	intc_cpu_trigger_interrupt(0, 3);
	check("an enabled software interrupt releases wfi", !core->is_halt);

	clock->clock = 100;
	device_supply_clock(clock);
	erbium_timer_ops.get_data32(NULL, 0, 0x80f40200ULL, &value);
	/* 100 clocks at 200 MHz are 0.5 us; mtime runs at 2 MHz: 1 tick. */
	check("mtime after 100 clocks at the default frequencies", value == 1U);
}

/*
 * A hand-made one-instruction image. With spin halt enabled, JAL x0, 0 and
 * C.J 0 retire once and halt, so the main-loop rule can skip to the next
 * device event. Disabled, the same architectural state is reached by
 * executing the loop for every clock.
 */
static void check_spin_halt(void)
{
	TargetCoreType *core;
	uint16 compressed_jump = 0xa001U;
	uint64 skipped_clock;
	uint64 register_hash;

	spin_halt_option = 1U;
	athrill_idle_spin_halt_init();
	core = reset_core();
	put_insn(0, 0x0000006fU);
	cpu_supply_clock(0);
	register_hash = core->reg.x[0] | core->reg.x[1] | core->reg.x[2];
	skipped_clock = core->is_halt ? 1000U : 1U;
	check("j . halts after one retirement and permits clock skip",
		core->is_halt && core->reg.pc == 0U && core->internal_cycle == 1U
		&& core->internal_instret == 1U && register_hash == 0U && skipped_clock == 1000U);

	spin_halt_option = 0U;
	athrill_idle_spin_halt_init();
	core = reset_core();
	put_insn(0, 0x0000006fU);
	for (skipped_clock = 0U; skipped_clock < 1000U; skipped_clock++) {
		cpu_supply_clock(0);
	}
	check("j . with the option off keeps executing with the same register and PC results",
		!core->is_halt && core->reg.pc == 0U && core->internal_cycle == 1000U
		&& core->internal_instret == 1000U
		&& (core->reg.x[0] | core->reg.x[1] | core->reg.x[2]) == register_hash);

	spin_halt_option = 1U;
	athrill_idle_spin_halt_init();
	core = reset_core();
	memcpy(&memory[0], &compressed_jump, sizeof(compressed_jump));
	cpu_supply_clock(0);
	check("c.j 0 halts after one retirement", core->is_halt && core->reg.pc == 0U
		&& core->internal_cycle == 1U && core->internal_instret == 1U);

	core = reset_core();
	put_insn(0, 0x0000006fU);
	core->reg.csr.mie = 1ULL << 3U;
	core->reg.csr.mip = 1ULL << 3U;
	cpu_supply_clock(0);
	check("a pending interrupt prevents spin halt", !core->is_halt && core->reg.pc == 0U);
}

static void check_plic_interrupt(void)
{
	TargetCoreType *core = reset_core();

	core->reg.csr.mtvec = 32;
	core->reg.csr.mstatus = 1ULL << 3;
	core->reg.csr.mie = 1ULL << 11;
	intc_plic_ops.put_data32(NULL, 0, 0xa0002000ULL, 1U << 4);
	check("intc_raise_intr accepts PLIC source 4", intc_raise_intr(4U) == 0);
	cpu_supply_clock(0);
	check("the PLIC source reaches the machine external interrupt trap",
		core->reg.csr.mcause == ((1ULL << 63) | 11U) && core->reg.pc == 32U);
}

int main(void)
{
	DeviceClockType clock;

	memset(memory, 0, sizeof(memory));
	virtual_cpu.core_id_num = 1;
	cpu_init();
	device_init(&virtual_cpu, &clock);
	check_counters_and_traps(&clock);
	check_modified_code();
	check_block_after_store();
	check_block_through_branch();
	check_block_counter_read();
	check_misaligned_doubleword();
	check_counter_csr();
	check_fence_i();
	check_wfi_and_timer(&clock);
	check_spin_halt();
	check_plic_interrupt();
	printf("%s\n", failures == 0U ? "SELFTEST PASS" : "SELFTEST FAIL");
	return failures == 0U ? 0 : 1;
}
