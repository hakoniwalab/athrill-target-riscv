/* Differential test for execute_block() and jit_execute_block(). */
#include "cpu.h"
#include "std_cpu_ops.h"
#include "std_device_ops.h"
#include "athrill_idle_spin_halt.h"
#include "riscv_decode.h"
#include "device_access.h"
#include "device_tree.h"
#include "mpu_ops.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAM_SIZE 16384U
#define CODE 0x100U
#define OTHER_CODE 0x200U
#define IDLE_PC 0x180U
#define DATA 0x1000U
#define MAX_INSNS 64U
#define RANDOM_SEED 0x6a09e667U

static uint8 memory[RAM_SIZE];
static uint8 initial_memory[RAM_SIZE];
static uint64 test_clock;
static unsigned failures;
static const char *active_case;

extern void riscv_jit_test_set_enabled(std_bool enabled);
extern void riscv_jit_test_get_counters(uint64 *, uint64 *, uint64 *, uint64 *, uint64 *, uint32 *, std_bool *);

std_bool riscv_device_tree_build(void) { return FALSE; }
void riscv_device_tree_tick(DeviceClockType *clock) { (void)clock; }
#ifdef EXDEV_ENABLE
void device_supply_clock_exdev(DeviceClockType *clock) { (void)clock; }
void device_init_athrill_exdev(void) {}
#endif

int cpu_config_get_core_id_num(void) { return 1; }
uint64 cpuemu_get_clock(void) { return test_clock; }
Std_ReturnType cpuemu_get_devcfg_value(const char *key, uint32 *value)
{
	if (strcmp(key, "DEBUG_FUNC_ENABLE_SKIP_CLOCK") == 0) {
		*value = 1U;
		return STD_E_OK;
	}
	if (strcmp(key, "DEVICE_CONFIG_SPIN_HALT") == 0) {
		*value = 1U;
		return STD_E_OK;
	}
	return STD_E_INVALID;
}
Std_ReturnType cpuemu_get_devcfg_string(const char *key, char **value)
{
	if (strcmp(key, "DEVICE_CONFIG_IDLE_FUNCTION") == 0) {
		*value = (char *)"jit_diff_idle";
		return STD_E_OK;
	}
	return STD_E_INVALID;
}
int symbol_get_func(char *name, uint32 length, AthrillAddrType *address, uint32 *size)
{
	(void)name;
	(void)length;
	*address = IDLE_PC;
	*size = 4U;
	return 0;
}

static Std_ReturnType range(AthrillAddrType address, uint32 size, uint8 **p)
{
	if ((address >= RAM_SIZE) || (size > RAM_SIZE - (uint32)address)) return STD_E_INVALID;
	*p = &memory[address];
	return STD_E_OK;
}
Std_ReturnType mpu_get_pointer(CoreIdType id, AthrillAddrType address, uint8 **p)
{
	(void)id;
	return range(address, 1U, p);
}
Std_ReturnType mpu_get_data8(CoreIdType id, AthrillAddrType a, uint8 *v)
{
	uint8 *p; (void)id; if (range(a, 1U, &p) != STD_E_OK) return STD_E_INVALID; *v = *p; return STD_E_OK;
}
Std_ReturnType mpu_get_data16(CoreIdType id, AthrillAddrType a, uint16 *v)
{
	uint8 *p; (void)id; if (range(a, 2U, &p) != STD_E_OK) return STD_E_INVALID; memcpy(v, p, 2U); return STD_E_OK;
}
Std_ReturnType mpu_get_data32(CoreIdType id, AthrillAddrType a, uint32 *v)
{
	uint8 *p; (void)id; if (range(a, 4U, &p) != STD_E_OK) return STD_E_INVALID; memcpy(v, p, 4U); return STD_E_OK;
}
Std_ReturnType mpu_put_data8(CoreIdType id, AthrillAddrType a, uint8 v)
{
	uint8 *p; (void)id; if (range(a, 1U, &p) != STD_E_OK) return STD_E_INVALID; *p = v; return STD_E_OK;
}
Std_ReturnType mpu_put_data16(CoreIdType id, AthrillAddrType a, uint16 v)
{
	uint8 *p; (void)id; if (range(a, 2U, &p) != STD_E_OK) return STD_E_INVALID; memcpy(p, &v, 2U); return STD_E_OK;
}
Std_ReturnType mpu_put_data32(CoreIdType id, AthrillAddrType a, uint32 v)
{
	uint8 *p; (void)id; if (range(a, 4U, &p) != STD_E_OK) return STD_E_INVALID; memcpy(p, &v, 4U); return STD_E_OK;
}
Std_ReturnType mpu_get_memory_window(CoreIdType id, AthrillAddrType a,
	AthrillAddrType *start, AthrillAddrType *size, uint8 **data, std_bool *writable)
{
	(void)id;
	if (a >= RAM_SIZE) return STD_E_NOENT;
	*start = 0U; *size = RAM_SIZE; *data = memory; *writable = TRUE;
	return STD_E_OK;
}

static uint32 r_insn(uint32 f7, uint32 rs2, uint32 rs1, uint32 f3, uint32 rd, uint32 op)
{
	return (f7 << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | op;
}
static uint32 i_insn(sint32 imm, uint32 rs1, uint32 f3, uint32 rd, uint32 op)
{
	return (((uint32)imm & 0xfffU) << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | op;
}
static uint32 s_insn(sint32 imm, uint32 rs2, uint32 rs1, uint32 f3)
{
	uint32 u = (uint32)imm & 0xfffU;
	return ((u & 0xfe0U) << 20) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | ((u & 31U) << 7) | 0x23U;
}
static uint32 u_insn(uint32 imm, uint32 rd, uint32 op) { return (imm & 0xfffff000U) | (rd << 7) | op; }
static uint32 b_insn(sint32 imm, uint32 rs2, uint32 rs1, uint32 f3)
{
	uint32 u = (uint32)imm & 0x1fffU;
	return ((u & 0x1000U) << 19) | ((u & 0x7e0U) << 20) | (rs2 << 20) |
		(rs1 << 15) | (f3 << 12) | ((u & 0x1eU) << 7) | ((u & 0x800U) >> 4) | 0x63U;
}
static uint32 j_insn(sint32 imm, uint32 rd)
{
	uint32 u = (uint32)imm & 0x1fffffU;
	return ((u & 0x100000U) << 11) | (u & 0xff000U) | ((u & 0x800U) << 9) |
		((u & 0x7feU) << 20) | (rd << 7) | 0x6fU;
}

enum Path { PATH_ANY, PATH_NATIVE, PATH_C };
typedef struct {
	const char *name;
	uint64 pc;
	uint32 insn[MAX_INSNS];
	uint8 length[MAX_INSNS];
	uint32 count;
	uint64 budget;
	uint64 x[32];
	RiscvPrivilege privilege;
	uint64 pmpcfg;
	uint64 pmpaddr;
	uint64 pmp_epoch;
	enum Path path;
	uint32 min_native;
	uint32 expected_done;
	sint8 expected_taken; /* -1: do not check, 0/1: native taken-branch count */
	sint8 expected_pending; /* -1: do not check, 0/1: unlinked chain exit */
	uint8 scenario;
} Plan;

typedef struct {
	CpuRegisterType reg;
	uint64 cycle, instret, clock, done, elaps, decode_hash;
	std_bool access_fault, halt;
	uint8 ram[RAM_SIZE];
	uint64 blocks, native, called, runtime_calls, taken_branches;
	uint32 links;
	std_bool pending_link;
} Result;

static uint64 hash_u64(uint64 h, uint64 v)
{
	unsigned i;
	for (i = 0; i < 8; i++) { h ^= (uint8)(v >> (i * 8U)); h *= 1099511628211ULL; }
	return h;
}

static uint64 decode_hash(TargetCoreType *core)
{
	uint64 h = 1469598103934665603ULL;
	uint32 d, n;
	for (d = 0; d < RISCV_DECODE_DIRECTORY_SIZE; d++) {
		RiscvDecodePage *p = core->decode_pages[d];
		if (p == NULL) continue;
		h = hash_u64(h, d); h = hash_u64(h, p->base); h = hash_u64(h, p->generation);
		for (n = 0; n < RISCV_DECODE_PAGE_ENTRIES; n++) {
			RiscvDecodedInsn *e = &p->entries[n];
			if (!e->valid && e->block_length == 0U) continue;
			h = hash_u64(h, n); h = hash_u64(h, e->valid); h = hash_u64(h, e->pc);
			h = hash_u64(h, e->instruction); h = hash_u64(h, e->raw);
			h = hash_u64(h, e->pmp_epoch); h = hash_u64(h, e->privilege);
			h = hash_u64(h, e->rd | (uint64)e->rs1 << 8 | (uint64)e->rs2 << 16 |
				(uint64)e->length << 24 | (uint64)e->block_length << 32);
			h = hash_u64(h, e->block_generation);
		}
	}
	return h;
}

static void put32(uint64 address, uint32 word) { memcpy(&memory[address], &word, 4U); }

static uint8 plan_length(const Plan *p, uint32 i) { return p->length[i] != 0U ? p->length[i] : 4U; }

static uint64 plan_pc(const Plan *p, uint32 index)
{
	uint32 i;
	uint64 pc = p->pc;
	for (i = 0U; i < index; i++) pc += plan_length(p, i);
	return pc;
}

static void decode_one(TargetCoreType *core, uint64 pc, uint8 length)
{
	RiscvDecodedInsn *e = riscv_decode_slot(core, pc);
	uint32 raw = 0U, expanded;
	memcpy(&raw, &memory[pc], length);
	expanded = raw;
	if ((length == 2U) && !riscv_expand_compressed((uint16)raw, &expanded)) {
		fprintf(stderr, "FAIL %s: cannot expand compressed %04x at 0x%" PRIx64 "\n", active_case, raw, pc);
		exit(2);
	}
	if ((e == NULL) || !riscv_decode_fill(pc, expanded, length, e)) {
		fprintf(stderr, "FAIL %s: cannot decode %08x at 0x%" PRIx64 "\n", active_case, raw, pc);
		exit(2);
	}
	e->code = &memory[pc]; e->raw = raw; e->pmp_epoch = core->pmp_epoch; e->privilege = core->reg.privilege;
}
static void decode_plan(TargetCoreType *core, const Plan *p)
{
	uint32 i;
	for (i = 0; i < p->count; i++) decode_one(core, plan_pc(p, i), plan_length(p, i));
}

static void init_core(TargetCoreType *core, const Plan *p)
{
	uint32 i;

	memcpy(memory, initial_memory, RAM_SIZE);
	for (i = 0U; i < p->count; i++) memcpy(&memory[plan_pc(p, i)], &p->insn[i], plan_length(p, i));
	if (p->scenario == 2U) put32(OTHER_CODE, i_insn(1, 5, 0, 5, 0x13U));
	cpu_reset(0);
	virtual_cpu.cores[0].elaps = 0U;
	core = &virtual_cpu.cores[0].core;
	core->reg.pc = p->pc;
	memcpy(core->reg.x, p->x, sizeof(p->x));
	core->reg.x[0] = 0U;
	core->reg.privilege = p->privilege;
	core->reg.csr.mtvec = 0x300U;
	core->reg.csr.pmpcfg[0] = p->pmpcfg;
	core->reg.csr.pmpaddr[0] = p->pmpaddr;
	core->pmp_epoch = p->pmp_epoch;
	core->memory_window.valid = TRUE;
	core->memory_window.writable = TRUE;
	core->memory_window.start = 0U;
	core->memory_window.end = RAM_SIZE;
	core->memory_window.data = memory;
	core->memory_window.pmp_valid = FALSE;
	decode_plan(core, p);
	if (p->scenario == 2U) decode_one(core, OTHER_CODE, 4U);
}

static Result run(const Plan *p, std_bool jit)
{
	TargetCoreType *core = &virtual_cpu.cores[0].core;
	Result r;
	uint64 b0, n0, c0, rc0, t0;
	uint64 b1, n1, c1, rc1, t1;
	uint32 links;
	std_bool pending;
	uint64 done = 0U, extra = 0U;

	active_case = p->name;
	init_core(core, p);
	core = &virtual_cpu.cores[0].core;
	riscv_jit_test_set_enabled(jit);
	riscv_jit_test_get_counters(&b0, &n0, &c0, &rc0, &t0, &links, &pending);
	test_clock = 7U;
	riscv_device_accessed = FALSE;
	(void)cpu_supply_clocks(0, p->budget, &test_clock, &done);
	if (p->scenario == 1U) {
		/* Execute the instruction overwritten in the running block. */
		core->reg.pc = p->pc + 4U;
		(void)cpu_supply_clocks(0, 1U, &test_clock, &extra);
		done += extra;
	} else if (p->scenario == 2U) {
		/* Execute a different block after the store invalidated it. */
		core->reg.pc = OTHER_CODE;
		(void)cpu_supply_clocks(0, 1U, &test_clock, &extra);
		done += extra;
	} else if (p->scenario == 3U) {
		/* Same compiled block, changed PMP epoch: refill as fetch would, then rerun. */
		core->reg.csr.pmpcfg[0] = 0x1cU; /* NAPOT, W+X, no R */
		core->pmp_epoch++;
		decode_plan(core, p);
		core->reg.pc = p->pc;
		(void)cpu_supply_clocks(0, p->budget, &test_clock, &extra);
		done += extra;
	}
	riscv_jit_test_get_counters(&b1, &n1, &c1, &rc1, &t1, &links, &pending);
	memset(&r, 0, sizeof(r));
	r.reg = core->reg; r.cycle = core->internal_cycle; r.instret = core->internal_instret;
	r.clock = test_clock; r.done = done; r.elaps = virtual_cpu.cores[0].elaps;
	r.access_fault = core->access_fault; r.halt = core->is_halt;
	r.decode_hash = decode_hash(core); memcpy(r.ram, memory, RAM_SIZE);
	r.blocks = b1 - b0; r.native = n1 - n0; r.called = c1 - c0; r.runtime_calls = rc1 - rc0;
	r.taken_branches = t1 - t0;
	r.links = links; r.pending_link = pending;
	return r;
}

static void fail_field(const char *name, const char *field)
{
	fprintf(stderr, "FAIL %s: interpreter/JIT differ in %s\n", name, field);
	failures++;
}

static void capture(Result *r, TargetCoreType *core, uint64 done,
	uint64 b0, uint64 n0, uint64 c0, uint64 rc0, uint64 t0)
{
	uint64 b1, n1, c1, rc1, t1;
	uint32 links;
	std_bool pending;

	memset(r, 0, sizeof(*r));
	riscv_jit_test_get_counters(&b1, &n1, &c1, &rc1, &t1, &links, &pending);
	r->reg = core->reg; r->cycle = core->internal_cycle; r->instret = core->internal_instret;
	r->clock = test_clock; r->done = done; r->elaps = virtual_cpu.cores[0].elaps;
	r->access_fault = core->access_fault; r->halt = core->is_halt; r->decode_hash = decode_hash(core);
	memcpy(r->ram, memory, RAM_SIZE);
	r->blocks = b1 - b0; r->native = n1 - n0; r->called = c1 - c0;
	r->runtime_calls = rc1 - rc0; r->taken_branches = t1 - t0;
	r->links = links; r->pending_link = pending;
}

static void compare_arch(const char *name, const Result *a, const Result *b)
{
	if (memcmp(&a->reg, &b->reg, sizeof(a->reg)) != 0) fail_field(name, "core->reg");
	if (a->cycle != b->cycle) fail_field(name, "internal_cycle");
	if (a->instret != b->instret) fail_field(name, "internal_instret");
	if (a->clock != b->clock) fail_field(name, "clock");
	if (a->done != b->done) fail_field(name, "returned count");
	if (a->access_fault != b->access_fault) fail_field(name, "access_fault");
	if (a->halt != b->halt) fail_field(name, "halt state");
	if (a->elaps != b->elaps) fail_field(name, "elapsed clocks");
	if (memcmp(a->ram, b->ram, RAM_SIZE) != 0) fail_field(name, "guest memory");
	if (a->decode_hash != b->decode_hash) fail_field(name, "decode cache");
}

static void compare_arch_without_decode_cache(const char *name, const Result *a, const Result *b)
{
	Result copy = *b;
	copy.decode_hash = a->decode_hash;
	compare_arch(name, a, &copy);
}

#define MULTI_STEPS 24U
typedef struct {
	uint64 budget;
	std_bool set_pc;
	uint64 pc;
} SupplyStep;

static void run_supplies(const Plan *p, const SupplyStep *steps, uint32 count, std_bool jit, Result *out)
{
	TargetCoreType *core = &virtual_cpu.cores[0].core;
	uint32 i;

	active_case = p->name;
	init_core(core, p);
	core = &virtual_cpu.cores[0].core;
	riscv_jit_test_set_enabled(jit);
	test_clock = 7U;
	riscv_device_accessed = FALSE;
	for (i = 0U; i < count; i++) {
		uint64 b0, n0, c0, rc0, t0, done = 0U;
		uint32 links;
		std_bool pending;
		if (steps[i].set_pc) core->reg.pc = steps[i].pc;
		riscv_jit_test_get_counters(&b0, &n0, &c0, &rc0, &t0, &links, &pending);
		(void)cpu_supply_clocks(0, steps[i].budget, &test_clock, &done);
		capture(&out[i], core, done, b0, n0, c0, rc0, t0);
	}
}

static void compare_supplies(const char *base, const Result *a, const Result *b, uint32 count)
{
	uint32 i;
	char name[128];
	for (i = 0U; i < count; i++) {
		snprintf(name, sizeof(name), "%s/supply-%u", base, i);
		compare_arch(name, &a[i], &b[i]);
	}
}

static void run_pair(const Plan *p)
{
	Result a = run(p, FALSE);
	Result b = run(p, TRUE);
	unsigned before = failures;
	if (memcmp(&a.reg, &b.reg, sizeof(a.reg)) != 0) fail_field(p->name, "core->reg");
	if (a.cycle != b.cycle) fail_field(p->name, "internal_cycle");
	if (a.instret != b.instret) fail_field(p->name, "internal_instret");
	if (a.clock != b.clock) fail_field(p->name, "clock");
	if (a.done != b.done) fail_field(p->name, "returned count");
	if (a.access_fault != b.access_fault) fail_field(p->name, "access_fault");
	if (a.halt != b.halt) fail_field(p->name, "halt state");
	if (a.elaps != b.elaps) fail_field(p->name, "elapsed clocks");
	if (memcmp(a.ram, b.ram, RAM_SIZE) != 0) fail_field(p->name, "guest memory");
	if (a.decode_hash != b.decode_hash) fail_field(p->name, "decode cache");
	if ((p->expected_done != 0U) && (b.done != p->expected_done)) fail_field(p->name, "expected block length");
	if ((p->path != PATH_ANY) && (b.blocks == 0U)) fail_field(p->name, "JIT block counter");
	if ((p->path == PATH_NATIVE) && ((b.native < p->min_native) || (b.runtime_calls != 0U)))
		fail_field(p->name, "native-path proof");
	if ((p->path == PATH_C) && (b.runtime_calls == 0U)) fail_field(p->name, "C-path proof");
	if ((p->expected_taken >= 0) && (b.taken_branches != (uint64)p->expected_taken))
		fail_field(p->name, "taken-branch counter");
	if ((p->expected_pending >= 0) && (b.pending_link != (std_bool)p->expected_pending))
		fail_field(p->name, "chain-exit counter");
	if (failures != before) {
		fprintf(stderr, "  JIT counters blocks=%" PRIu64 " native=%" PRIu64 " called=%" PRIu64
			" runtime-calls=%" PRIu64 " taken-branches=%" PRIu64 "\n",
			b.blocks, b.native, b.called, b.runtime_calls, b.taken_branches);
	}
}

static Plan basic(const char *name, uint32 instruction, uint64 a, uint64 b, uint32 rd, enum Path path)
{
	Plan p;
	memset(&p, 0, sizeof(p));
	p.name = name; p.pc = CODE; p.insn[0] = instruction; p.insn[1] = i_insn(1, 31, 0, 31, 0x13U);
	p.count = 2U; p.budget = 2U; p.x[5] = a; p.x[7] = b; p.x[31] = 9U;
	p.privilege = RISCV_PRIV_M; p.path = path; p.min_native = 2U; p.expected_done = 2U;
	p.expected_taken = -1;
	p.expected_pending = -1;
	put32(CODE, p.insn[0]); put32(CODE + 4U, p.insn[1]);
	return p;
}

static void alu_cases(void)
{
	static const uint64 values[] = {0U, 1U, UINT64_MAX, 0x8000000000000000ULL, 0x80000000U, 0x7fffffffU};
	struct Op { const char *name; uint32 f7, f3, op; } ops[] = {
		{"add",0,0,0x33},{"sub",0x20,0,0x33},{"mul",1,0,0x33},
		{"div",1,4,0x33},{"divu",1,5,0x33},{"rem",1,6,0x33},{"remu",1,7,0x33},
		{"addw",0,0,0x3b},{"subw",0x20,0,0x3b},
		{"and",0,7,0x33},{"or",0,6,0x33},{"xor",0,4,0x33}
	};
	uint32 i, j, rd_modes[] = {6U, 0U, 5U};
	char names[sizeof(ops)/sizeof(ops[0])][sizeof(values)/sizeof(values[0])][3][64];
	for (i = 0; i < sizeof(ops)/sizeof(ops[0]); i++) for (j = 0; j < sizeof(values)/sizeof(values[0]); j++) {
		uint32 m;
		for (m = 0; m < 3; m++) {
			Plan p; snprintf(names[i][j][m], 64, "alu/%s/v%u/%s", ops[i].name, j,
				m == 0 ? "rd" : (m == 1 ? "x0" : "rd-rs1"));
			p = basic(names[i][j][m], r_insn(ops[i].f7, 7, 5, ops[i].f3, rd_modes[m], ops[i].op),
				values[j], values[(j + 2U) % (sizeof(values)/sizeof(values[0]))], rd_modes[m], PATH_NATIVE);
			run_pair(&p);
		}
	}
	/* Immediate and specialised native forms. */
	for (j = 0; j < sizeof(values)/sizeof(values[0]); j++) {
		Plan p;
		char name[64];
		snprintf(name, sizeof(name), "alu/addi/boundary-%u", j);
		p = basic(name, i_insn((j & 1U) ? -1 : 1, 5, 0, 5, 0x13U), values[j], 0, 5, PATH_NATIVE); run_pair(&p);
		snprintf(name, sizeof(name), "alu/addiw/boundary-%u", j);
		p = basic(name, i_insn((j & 1U) ? -1 : 1, 5, 0, 5, 0x1bU), values[j], 0, 5, PATH_NATIVE); run_pair(&p);
		snprintf(name, sizeof(name), "alu/li/boundary-%u", j);
		p = basic(name, i_insn(-1, 0, 0, 6, 0x13U), values[j], 0, 6, PATH_NATIVE); run_pair(&p);
		snprintf(name, sizeof(name), "alu/mv/boundary-%u", j);
		p = basic(name, i_insn(0, 5, 0, 6, 0x13U), values[j], 0, 6, PATH_NATIVE); run_pair(&p);
	}
	{
		Plan p;
		p = basic("alu/div/minus-one-overflow", r_insn(1,7,5,4,6,0x33), 0x8000000000000000ULL, UINT64_MAX, 6, PATH_NATIVE); run_pair(&p);
		p = basic("alu/divu/minus-one", r_insn(1,7,5,5,6,0x33), 0x8000000000000000ULL, UINT64_MAX, 6, PATH_NATIVE); run_pair(&p);
		p = basic("alu/rem/minus-one-overflow", r_insn(1,7,5,6,6,0x33), 0x8000000000000000ULL, UINT64_MAX, 6, PATH_NATIVE); run_pair(&p);
		p = basic("alu/remu/minus-one", r_insn(1,7,5,7,6,0x33), 0x8000000000000000ULL, UINT64_MAX, 6, PATH_NATIVE); run_pair(&p);
		p = basic("alu/lui", u_insn(0x80000000U, 6, 0x37U), 0, 0, 6, PATH_NATIVE); run_pair(&p);
		p = basic("alu/auipc", u_insn(0x7ffff000U, 6, 0x17U), 0, 0, 6, PATH_NATIVE); run_pair(&p);
		p = basic("alu/discard", i_insn(1, 5, 0, 0, 0x13U), UINT64_MAX, 0, 0, PATH_NATIVE); run_pair(&p);
	}
	/* W M operations are differential coverage; this implementation calls them. */
	{
		struct Op wm[] = {{"mulw",1,0,0x3b},{"divw",1,4,0x3b},{"divuw",1,5,0x3b},{"remw",1,6,0x3b},{"remuw",1,7,0x3b}};
		for (i = 0; i < sizeof(wm)/sizeof(wm[0]); i++) {
			Plan p = basic(wm[i].name, r_insn(wm[i].f7,7,5,wm[i].f3,6,wm[i].op), 0x80000000U, UINT64_MAX, 6, PATH_C);
			run_pair(&p);
		}
	}
}

static void shift_logic_cases(void)
{
	static const uint32 counts[] = {0,31,32,63,64,95,127,255};
	struct Shift { const char *name; uint32 f7, f3, op; std_bool word; } s[] = {
		{"sll",0,1,0x33,FALSE},{"srl",0,5,0x33,FALSE},{"sra",0x20,5,0x33,FALSE},
		{"sllw",0,1,0x3b,TRUE},{"srlw",0,5,0x3b,TRUE},{"sraw",0x20,5,0x3b,TRUE}
	};
	uint32 i,j;
	for (i=0;i<sizeof(s)/sizeof(s[0]);i++) for (j=0;j<sizeof(counts)/sizeof(counts[0]);j++) {
		char name[64]; Plan p;
		snprintf(name,sizeof(name),"shift/%s/%u",s[i].name,counts[j]);
		p=basic(name,r_insn(s[i].f7,7,5,s[i].f3,5,s[i].op),0x800000007fffffffULL,counts[j],5,PATH_NATIVE); run_pair(&p);
		snprintf(name,sizeof(name),"shift/%s/%u/x0",s[i].name,counts[j]);
		p=basic(name,r_insn(s[i].f7,7,5,s[i].f3,0,s[i].op),0x800000007fffffffULL,counts[j],0,PATH_NATIVE); run_pair(&p);
	}
	for (j=0;j<4;j++) {
		uint32 c=counts[j]; char name[64]; Plan p;
		snprintf(name,sizeof(name),"shift/slli/%u",c); p=basic(name,i_insn((sint32)c,5,1,6,0x13),0x800000007fffffffULL,0,6,PATH_NATIVE); run_pair(&p);
		snprintf(name,sizeof(name),"shift/slli/%u/x0",c); p=basic(name,i_insn((sint32)c,5,1,0,0x13),0x800000007fffffffULL,0,0,PATH_NATIVE); run_pair(&p);
		snprintf(name,sizeof(name),"shift/srli/%u",c); p=basic(name,i_insn((sint32)c,5,5,6,0x13),0x800000007fffffffULL,0,6,PATH_NATIVE); run_pair(&p);
		snprintf(name,sizeof(name),"shift/srli/%u/x0",c); p=basic(name,i_insn((sint32)c,5,5,0,0x13),0x800000007fffffffULL,0,0,PATH_NATIVE); run_pair(&p);
		snprintf(name,sizeof(name),"shift/srai/%u",c); p=basic(name,i_insn((sint32)(c|0x400),5,5,6,0x13),0x800000007fffffffULL,0,6,PATH_NATIVE); run_pair(&p);
		snprintf(name,sizeof(name),"shift/srai/%u/x0",c); p=basic(name,i_insn((sint32)(c|0x400),5,5,0,0x13),0x800000007fffffffULL,0,0,PATH_NATIVE); run_pair(&p);
		if(c<32){
			snprintf(name,sizeof(name),"shift/slliw/%u",c); p=basic(name,i_insn((sint32)c,5,1,6,0x1b),0x800000007fffffffULL,0,6,PATH_NATIVE); run_pair(&p);
			snprintf(name,sizeof(name),"shift/slliw/%u/x0",c); p=basic(name,i_insn((sint32)c,5,1,0,0x1b),0x800000007fffffffULL,0,0,PATH_NATIVE); run_pair(&p);
			snprintf(name,sizeof(name),"shift/srliw/%u",c); p=basic(name,i_insn((sint32)c,5,5,6,0x1b),0x800000007fffffffULL,0,6,PATH_NATIVE); run_pair(&p);
			snprintf(name,sizeof(name),"shift/srliw/%u/x0",c); p=basic(name,i_insn((sint32)c,5,5,0,0x1b),0x800000007fffffffULL,0,0,PATH_NATIVE); run_pair(&p);
			snprintf(name,sizeof(name),"shift/sraiw/%u",c); p=basic(name,i_insn((sint32)(c|0x400),5,5,6,0x1b),0x800000007fffffffULL,0,6,PATH_NATIVE); run_pair(&p);
			snprintf(name,sizeof(name),"shift/sraiw/%u/x0",c); p=basic(name,i_insn((sint32)(c|0x400),5,5,0,0x1b),0x800000007fffffffULL,0,0,PATH_NATIVE); run_pair(&p);
		}
	}
	{
		struct L {const char *n;uint32 f;} l[]={{"andi",7},{"ori",6},{"xori",4}};
		for(i=0;i<3;i++){char name[32];Plan p;snprintf(name,sizeof(name),"logic/%s",l[i].n);p=basic(name,i_insn(-1,5,l[i].f,5,0x13),0x800000007fffffffULL,0,5,PATH_NATIVE);run_pair(&p);snprintf(name,sizeof(name),"logic/%s/x0",l[i].n);p=basic(name,i_insn(-1,5,l[i].f,0,0x13),0x800000007fffffffULL,0,0,PATH_NATIVE);run_pair(&p);}
	}
}

static void branch_last_case(const char *name, uint32 f3, uint32 rs1, uint32 rs2,
	uint64 a, uint64 b, sint32 immediate, std_bool taken, enum Path path)
{
	Plan p = basic(name, b_insn(immediate, rs2, rs1, f3), a, b, 0U, path);
	p.count = 1U; p.budget = 1U; p.min_native = path == PATH_NATIVE ? 1U : 0U;
	p.expected_done = 1U; p.expected_taken = path == PATH_NATIVE ? (sint8)taken : -1;
	p.x[rs1] = a; p.x[rs2] = b;
	run_pair(&p);
}

static void branch_cases(void)
{
	struct Branch { const char *name; uint32 f3; uint64 ta, tb, fa, fb; } branches[] = {
		{"beq", 0U, 0x8000000000000000ULL, 0x8000000000000000ULL, 0U, UINT64_MAX},
		{"bne", 1U, 0U, UINT64_MAX, 0x7fffffffffffffffULL, 0x7fffffffffffffffULL},
		{"blt", 4U, 0x8000000000000000ULL, 0x7fffffffffffffffULL, 0x7fffffffffffffffULL, 0x8000000000000000ULL},
		{"bge", 5U, 0x7fffffffffffffffULL, 0x8000000000000000ULL, 0x8000000000000000ULL, 0x7fffffffffffffffULL},
		{"bltu", 6U, 0U, UINT64_MAX, UINT64_MAX, 0U},
		{"bgeu", 7U, UINT64_MAX, 0U, 0U, UINT64_MAX}
	};
	uint32 i;
	for (i = 0U; i < sizeof(branches) / sizeof(branches[0]); i++) {
		char name[80];
		Plan p;
		snprintf(name, sizeof(name), "branch/%s/taken-middle-boundary", branches[i].name);
		p = basic(name, i_insn(1, 8, 0, 8, 0x13U), 0U, 0U, 8U, PATH_NATIVE);
		p.insn[1] = b_insn(8, 7U, 5U, branches[i].f3);
		p.insn[2] = i_insn(1, 9, 0, 9, 0x13U); /* skipped only when the branch is taken */
		p.insn[3] = j_insn(-12, 0U);
		p.count = 4U; p.budget = 4U; p.min_native = 4U; p.expected_done = 4U;
		p.expected_taken = 1; p.x[5] = branches[i].ta; p.x[7] = branches[i].tb;
		run_pair(&p);

		snprintf(name, sizeof(name), "branch/%s/not-taken-middle-boundary", branches[i].name);
		p.name = name; p.expected_taken = 0; p.x[5] = branches[i].fa; p.x[7] = branches[i].fb;
		run_pair(&p);

		snprintf(name, sizeof(name), "branch/%s/last-rs1-equals-rs2", branches[i].name);
		branch_last_case(name, branches[i].f3, 5U, 5U, UINT64_MAX, UINT64_MAX, 8,
			(branches[i].f3 == 0U) || (branches[i].f3 == 5U) || (branches[i].f3 == 7U), PATH_NATIVE);
		snprintf(name, sizeof(name), "branch/%s/last-x0-operands", branches[i].name);
		branch_last_case(name, branches[i].f3, 0U, 0U, 0U, 0U, 8,
			(branches[i].f3 == 0U) || (branches[i].f3 == 5U) || (branches[i].f3 == 7U), PATH_NATIVE);
	}
	branch_last_case("branch/beq/taken-target-next", 0U, 5U, 7U, 42U, 42U, 4, TRUE, PATH_NATIVE);
	branch_last_case("branch/beq/to-self-c-path", 0U, 5U, 7U, 1U, 1U, 0, TRUE, PATH_C);
	{
		Plan p = basic("branch/beq/idle-target-not-chained", b_insn((sint32)(IDLE_PC - CODE), 7U, 5U, 0U),
			9U, 9U, 0U, PATH_NATIVE);
		p.count = 1U; p.budget = 1U; p.min_native = 1U; p.expected_done = 1U;
		p.expected_taken = 1; p.expected_pending = 0;
		run_pair(&p);
	}
}

static void jump_case(const char *name, uint32 instruction, uint32 rd, uint32 rs1,
	uint64 rs1_value, enum Path path)
{
	Plan p = basic(name, instruction, 0U, 0U, rd, path);
	p.count = 1U; p.budget = 1U; p.min_native = path == PATH_NATIVE ? 1U : 0U;
	p.expected_done = 1U; p.x[rs1] = rs1_value;
	run_pair(&p);
}

static void jump_cases(void)
{
	Plan p;
	jump_case("jump/jal/rd-x0", j_insn(8, 0U), 0U, 5U, 0U, PATH_NATIVE);
	jump_case("jump/jal/rd-ra", j_insn(8, 1U), 1U, 5U, 0U, PATH_NATIVE);
	jump_case("jump/jal/to-self-c-path-spin-halt", j_insn(0, 0U), 0U, 5U, 0U, PATH_C);
	jump_case("jump/jalr/rd-x0", i_insn(0, 5U, 0U, 0U, 0x67U), 0U, 5U, CODE + 32U, PATH_NATIVE);
	jump_case("jump/jalr/rd-ra", i_insn(0, 5U, 0U, 1U, 0x67U), 1U, 5U, CODE + 32U, PATH_NATIVE);
	jump_case("jump/jalr/rd-equals-rs1-old-target", i_insn(0, 5U, 0U, 5U, 0x67U), 5U, 5U,
		CODE + 32U, PATH_NATIVE);
	jump_case("jump/jalr/odd-target-bit-zero-cleared", i_insn(0, 5U, 0U, 1U, 0x67U), 1U, 5U,
		CODE + 33U, PATH_NATIVE);
	jump_case("jump/jalr/to-self-c-path", i_insn(0, 5U, 0U, 0U, 0x67U), 0U, 5U, CODE, PATH_C);

	p = basic("jump/jal/idle-target-not-chained", j_insn((sint32)(IDLE_PC - CODE), 0U), 0U, 0U, 0U, PATH_NATIVE);
	p.count = 1U; p.budget = 1U; p.min_native = 1U; p.expected_done = 1U; p.expected_pending = 0;
	run_pair(&p);

	p = basic("jump/compressed-c.j/length-two", 0xa009U, 0U, 0U, 0U, PATH_NATIVE); /* c.j +2 */
	p.count = 1U; p.length[0] = 2U; p.budget = 1U; p.min_native = 1U; p.expected_done = 1U;
	run_pair(&p);
	p = basic("jump/compressed-c.jr/length-two", 0x8282U, 0U, 0U, 0U, PATH_NATIVE); /* c.jr x5 */
	p.count = 1U; p.length[0] = 2U; p.budget = 1U; p.min_native = 1U; p.expected_done = 1U; p.x[5] = CODE + 32U;
	run_pair(&p);
	p = basic("jump/compressed-c.jalr/length-two", 0x9282U, 0U, 0U, 0U, PATH_NATIVE); /* c.jalr x5 */
	p.count = 1U; p.length[0] = 2U; p.budget = 1U; p.min_native = 1U; p.expected_done = 1U; p.x[5] = CODE + 32U;
	run_pair(&p);
	p = basic("jump/compressed-c.j/to-self-c-path-spin-halt", 0xa001U, 0U, 0U, 0U, PATH_C); /* c.j 0 */
	p.count = 1U; p.length[0] = 2U; p.budget = 1U; p.min_native = 0U; p.expected_done = 1U;
	run_pair(&p);
}

static Plan three_block_plan(const char *name, std_bool loop)
{
	Plan p = basic(name, i_insn(1, 5U, 0U, 5U, 0x13U), 0U, 0U, 5U, PATH_NATIVE);
	p.insn[1] = j_insn(4, 0U);
	p.insn[2] = i_insn(1, 6U, 0U, 6U, 0x13U);
	p.insn[3] = j_insn(4, 0U);
	p.insn[4] = i_insn(1, 7U, 0U, 7U, 0x13U);
	p.insn[5] = j_insn(loop ? -20 : 4, 0U);
	p.count = 6U; p.budget = 2U; p.min_native = 2U; p.expected_done = 2U;
	return p;
}

static void chaining_cases(void)
{
	static Result interp[MULTI_STEPS], jit[MULTI_STEPS];
	unsigned before = failures;
	Plan p = three_block_plan("chaining/budget-boundaries", FALSE);
	SupplyStep boundaries[] = {
		{2U,FALSE,0U}, {2U,FALSE,0U}, {2U,FALSE,0U}, /* first pass compiles all blocks */
		{2U,TRUE,CODE}, {2U,FALSE,0U}, {2U,FALSE,0U}, /* second pass links B0 -> B1 -> B2 */
		{1U,TRUE,CODE}, {2U,TRUE,CODE}, {3U,TRUE,CODE},
		{4U,TRUE,CODE}, {5U,TRUE,CODE}, {6U,TRUE,CODE}
	};
	uint32 n = (uint32)(sizeof(boundaries) / sizeof(boundaries[0]));

	run_supplies(&p, boundaries, n, FALSE, interp);
	run_supplies(&p, boundaries, n, TRUE, jit);
	compare_supplies(p.name, interp, jit, n);
	if (jit[5].links < 2U) fail_field(p.name, "link-count proof");
	if ((jit[9].blocks != 1U) || (jit[9].done != 4U)) fail_field(p.name, "two-block chained-run proof");
	if ((jit[11].blocks != 1U) || (jit[11].done != 6U)) fail_field(p.name, "three-block chained-run proof");
	if (failures != before) return;

	p = three_block_plan("chaining/three-block-loop", TRUE);
	{
		SupplyStep loop_steps[] = {
			{2U,FALSE,0U}, {2U,FALSE,0U}, {2U,FALSE,0U}, {2U,FALSE,0U},
			{2U,FALSE,0U}, {2U,FALSE,0U},
			{18U,FALSE,0U}, {18U,FALSE,0U}, {18U,FALSE,0U}, {18U,FALSE,0U},
			{7U,FALSE,0U}
		};
		n = (uint32)(sizeof(loop_steps) / sizeof(loop_steps[0]));
		run_supplies(&p, loop_steps, n, FALSE, interp);
		run_supplies(&p, loop_steps, n, TRUE, jit);
		compare_supplies(p.name, interp, jit, n);
		if (jit[5].links < 3U) fail_field(p.name, "loop-link-count proof");
		if ((jit[6].blocks != 1U) || (jit[6].done != 18U)) fail_field(p.name, "loop-chain-use proof");
	}
}

static void one_supply(TargetCoreType *core, uint64 budget, Result *out)
{
	uint64 b0, n0, c0, rc0, t0, done = 0U;
	uint32 links;
	std_bool pending;
	riscv_jit_test_get_counters(&b0, &n0, &c0, &rc0, &t0, &links, &pending);
	(void)cpu_supply_clocks(0, budget, &test_clock, &done);
	capture(out, core, done, b0, n0, c0, rc0, t0);
}

static Plan two_block_loop(const char *name)
{
	Plan p = basic(name, i_insn(1, 5U, 0U, 5U, 0x13U), 0U, 0U, 5U, PATH_NATIVE);
	p.insn[1] = j_insn(4, 0U);
	p.insn[2] = i_insn(1, 6U, 0U, 6U, 0x13U);
	p.insn[3] = j_insn(-12, 0U);
	p.count = 4U; p.budget = 2U; p.min_native = 2U; p.expected_done = 2U;
	return p;
}

static void run_store_link_drop(std_bool jit_enabled, Result *out)
{
	Plan p = two_block_loop("link-drop/store-into-destination-code");
	TargetCoreType *core;
	uint32 i;
	p.insn[0] = s_insn(0, 11U, 10U, 2U);
	p.x[10] = DATA; p.x[11] = 0x11223344U;
	active_case = p.name; init_core(&virtual_cpu.cores[0].core, &p);
	core = &virtual_cpu.cores[0].core; riscv_jit_test_set_enabled(jit_enabled); test_clock = 7U;
	for (i = 0U; i < 4U; i++) one_supply(core, 2U, &out[i]);
	core->reg.pc = CODE; core->reg.x[10] = CODE + 8U;
	core->reg.x[11] = i_insn(16, 5U, 0U, 5U, 0x13U);
	one_supply(core, 4U, &out[4]);
}

static void run_pmp_link_drop(std_bool jit_enabled, Result *out)
{
	Plan p = two_block_loop("link-drop/pmp-change");
	TargetCoreType *core;
	uint32 i;
	active_case = p.name; init_core(&virtual_cpu.cores[0].core, &p);
	core = &virtual_cpu.cores[0].core; riscv_jit_test_set_enabled(jit_enabled); test_clock = 7U;
	for (i = 0U; i < 4U; i++) one_supply(core, 2U, &out[i]);
	core->pmp_epoch++;
	core->reg.pc = CODE;
	one_supply(core, 2U, &out[4]);
}

static void run_privilege_link_drop(std_bool jit_enabled, Result *out)
{
	Plan p = two_block_loop("link-drop/privilege-change-through-mret");
	TargetCoreType *core;
	uint32 i;
	p.insn[4] = 0x30200073U; /* mret */
	p.count = 5U;
	p.pmpcfg = 0x9fU; p.pmpaddr = (RAM_SIZE >> 3) - 1U; p.pmp_epoch = 41U;
	active_case = p.name; init_core(&virtual_cpu.cores[0].core, &p);
	core = &virtual_cpu.cores[0].core; riscv_jit_test_set_enabled(jit_enabled); test_clock = 7U;
	for (i = 0U; i < 4U; i++) one_supply(core, 2U, &out[i]);
	core->reg.pc = CODE + 16U; core->reg.csr.mepc = CODE;
	core->reg.csr.mstatus &= ~(3ULL << 11U); /* mret to U mode */
	one_supply(core, 1U, &out[4]);
	one_supply(core, 1U, &out[5]);
}

static void link_drop_cases(void)
{
	static Result interp[6], jit[6];
	uint32 i;
	char name[128];

	run_store_link_drop(FALSE, interp); run_store_link_drop(TRUE, jit);
	for (i = 0U; i < 5U; i++) { snprintf(name, sizeof(name), "link-drop/store-into-destination-code/supply-%u", i); compare_arch(name, &interp[i], &jit[i]); }
	if (jit[3].links < 2U) fail_field("link-drop/store-into-destination-code", "precondition links");
	if (jit[4].links != 0U) fail_field("link-drop/store-into-destination-code", "links dropped");

	run_pmp_link_drop(FALSE, interp); run_pmp_link_drop(TRUE, jit);
	for (i = 0U; i < 5U; i++) { snprintf(name, sizeof(name), "link-drop/pmp-change/supply-%u", i); compare_arch(name, &interp[i], &jit[i]); }
	if (jit[3].links < 2U) fail_field("link-drop/pmp-change", "precondition links");
	if (jit[4].links != 0U) fail_field("link-drop/pmp-change", "links dropped");

	run_privilege_link_drop(FALSE, interp); run_privilege_link_drop(TRUE, jit);
	for (i = 0U; i < 6U; i++) { snprintf(name, sizeof(name), "link-drop/privilege-change-through-mret/supply-%u", i); compare_arch(name, &interp[i], &jit[i]); }
	if (jit[3].links < 2U) fail_field("link-drop/privilege-change-through-mret", "precondition links");
	if (jit[5].links != 0U) fail_field("link-drop/privilege-change-through-mret", "links dropped");
}

static void memory_case(const char *name, uint32 instruction, uint64 address, uint64 value, enum Path path, uint8 scenario)
{
	Plan p = basic(name, instruction, 0, 0, 6, path);
	p.x[10]=address; p.x[11]=value; p.scenario=scenario;
	if(scenario!=0U) p.expected_done=3U;
	run_pair(&p);
}

static void memory_cases(void)
{
	struct Load {const char*n;uint32 f;uint32 w;} loads[]={{"lb",0,1},{"lh",1,2},{"lw",2,4},{"ld",3,8},{"lbu",4,1},{"lhu",5,2},{"lwu",6,4}};
	struct Store {const char*n;uint32 f;uint32 w;} stores[]={{"sb",0,1},{"sh",1,2},{"sw",2,4},{"sd",3,8}};
	uint32 i; char name[96];
	for(i=0;i<sizeof(loads)/sizeof(loads[0]);i++){
		snprintf(name,sizeof(name),"memory/%s/aligned",loads[i].n); memory_case(name,i_insn(0,10,loads[i].f,6,0x03),DATA+64,0,PATH_NATIVE,0);
		snprintf(name,sizeof(name),"memory/%s/first",loads[i].n); memory_case(name,i_insn(0,10,loads[i].f,6,0x03),0,0,PATH_NATIVE,0);
		snprintf(name,sizeof(name),"memory/%s/last-fitting",loads[i].n); memory_case(name,i_insn(0,10,loads[i].f,6,0x03),RAM_SIZE-loads[i].w,0,PATH_NATIVE,0);
		if(loads[i].w>1){snprintf(name,sizeof(name),"memory/%s/misaligned",loads[i].n);memory_case(name,i_insn(0,10,loads[i].f,6,0x03),DATA+65,0,PATH_NATIVE,0);}
		snprintf(name,sizeof(name),"memory/%s/cross-end",loads[i].n); memory_case(name,i_insn(0,10,loads[i].f,6,0x03),RAM_SIZE-loads[i].w+1U,0,PATH_C,0);
	}
	for(i=0;i<sizeof(stores)/sizeof(stores[0]);i++){
		snprintf(name,sizeof(name),"memory/%s/aligned",stores[i].n); memory_case(name,s_insn(0,11,10,stores[i].f),DATA+128,0x8877665544332211ULL,PATH_NATIVE,0);
		snprintf(name,sizeof(name),"memory/%s/first",stores[i].n); memory_case(name,s_insn(0,11,10,stores[i].f),0,0x8877665544332211ULL,PATH_C,0);
		snprintf(name,sizeof(name),"memory/%s/last-fitting",stores[i].n); memory_case(name,s_insn(0,11,10,stores[i].f),RAM_SIZE-stores[i].w,0x8877665544332211ULL,PATH_NATIVE,0);
		if(stores[i].w>1){snprintf(name,sizeof(name),"memory/%s/misaligned",stores[i].n);memory_case(name,s_insn(0,11,10,stores[i].f),DATA+129,0x8877665544332211ULL,PATH_NATIVE,0);}
		snprintf(name,sizeof(name),"memory/%s/cross-end",stores[i].n); memory_case(name,s_insn(0,11,10,stores[i].f),RAM_SIZE-stores[i].w+1U,0x8877665544332211ULL,PATH_C,0);
	}
	memory_case("memory/load-x0",i_insn(0,10,3,0,0x03),DATA+64,0,PATH_C,0);
	memory_case("memory/store-x0",s_insn(0,0,10,3),DATA+160,0,PATH_NATIVE,0);
	memory_case("memory/unmapped-load",i_insn(0,10,3,6,0x03),RAM_SIZE+32U,0,PATH_C,0);
	memory_case("memory/unmapped-store",s_insn(0,11,10,3),RAM_SIZE+32U,1,PATH_C,0);
}

static void pmp_cases(void)
{
	Plan p;
	/* One locked NAPOT entry covers the 16 KiB RAM. */
	p=basic("pmp/read-forbidden",i_insn(0,10,3,6,0x03),0,0,6,PATH_C);p.x[10]=DATA;p.privilege=RISCV_PRIV_U;p.pmpcfg=0x9eU;p.pmpaddr=(RAM_SIZE>>3)-1U;p.pmp_epoch=11;run_pair(&p);
	p=basic("pmp/write-forbidden",s_insn(0,11,10,3),0,0,6,PATH_C);p.x[10]=DATA;p.x[11]=1;p.privilege=RISCV_PRIV_U;p.pmpcfg=0x9dU;p.pmpaddr=(RAM_SIZE>>3)-1U;p.pmp_epoch=12;run_pair(&p);
	p=basic("pmp/window-refresh",i_insn(1,31,0,31,0x13),0,0,6,PATH_C);p.insn[1]=i_insn(0,10,3,6,0x03);p.x[10]=DATA;p.privilege=RISCV_PRIV_U;p.pmpcfg=0x9fU;p.pmpaddr=(RAM_SIZE>>3)-1U;p.pmp_epoch=13;p.scenario=3;p.expected_done=4;run_pair(&p);
}

static void code_store_cases(void)
{
	Plan p=basic("code-store/running-block",s_insn(0,11,10,2),0,0,6,PATH_C);
	p.insn[1]=i_insn(1,5,0,5,0x13);p.count=3;p.insn[2]=i_insn(2,6,0,6,0x13);p.budget=3;p.x[10]=CODE+4;p.x[11]=i_insn(16,5,0,5,0x13);p.scenario=1;p.expected_done=4;put32(CODE+4,p.insn[1]);put32(CODE+8,p.insn[2]);run_pair(&p);
	p=basic("code-store/other-block",s_insn(0,11,10,2),0,0,6,PATH_C);p.x[10]=OTHER_CODE;p.x[11]=i_insn(16,5,0,5,0x13);p.scenario=2;p.expected_done=3;put32(OTHER_CODE,i_insn(1,5,0,5,0x13));
	run_pair(&p);
}

static void early_end_cases(void)
{
	Plan p=basic("early/trap-middle",0x00000073U,0,0,6,PATH_C);p.insn[0]=i_insn(1,5,0,5,0x13);p.insn[1]=0x00000073U;p.insn[2]=i_insn(1,6,0,6,0x13);p.count=3;p.budget=3;p.expected_done=3;put32(CODE,p.insn[0]);put32(CODE+4,p.insn[1]);put32(CODE+8,p.insn[2]);run_pair(&p);
	p=basic("early/budget-smaller",i_insn(1,5,0,5,0x13),0,0,5,PATH_ANY);p.count=3;p.insn[1]=i_insn(1,6,0,6,0x13);p.insn[2]=i_insn(1,7,0,7,0x13);p.budget=2;p.expected_done=2;put32(CODE+4,p.insn[1]);put32(CODE+8,p.insn[2]);run_pair(&p);
	p=basic("early/idle-pc",i_insn(1,5,0,5,0x13),0,0,5,PATH_NATIVE);p.pc=IDLE_PC-8U;p.count=4;p.insn[0]=i_insn(1,5,0,5,0x13);p.insn[1]=i_insn(1,6,0,6,0x13);p.insn[2]=i_insn(1,7,0,7,0x13);p.insn[3]=i_insn(1,8,0,8,0x13);p.budget=4;p.expected_done=0;p.min_native=4;put32(p.pc,p.insn[0]);put32(p.pc+4,p.insn[1]);put32(p.pc+8,p.insn[2]);put32(p.pc+12,p.insn[3]);run_pair(&p);
}

static uint32 rng_state=RANDOM_SEED;
static uint32 random32(void){rng_state^=rng_state<<13;rng_state^=rng_state>>17;rng_state^=rng_state<<5;return rng_state;}
static void random_cases(void)
{
	static Result interp[10], jit[10];
	uint32 program,i; char name[80];
	for(program=0;program<1000U;program++){
		Plan p; SupplyStep steps[10]; uint32 k; unsigned before = failures;
		memset(&p,0,sizeof(p));snprintf(name,sizeof(name),"random/seed-%08x/program-%u",RANDOM_SEED,program);p.name=name;p.pc=CODE;p.count=8;p.budget=8;p.privilege=RISCV_PRIV_M;p.path=PATH_NATIVE;p.min_native=1;p.expected_done=8;p.expected_taken=-1;p.expected_pending=-1;
		p.x[5]=((uint64)random32()<<32)|random32();p.x[6]=((uint64)random32()<<32)|random32();p.x[7]=((uint64)random32()<<32)|random32();p.x[8]=((uint64)random32()<<32)|random32();p.x[9]=((uint64)random32()<<32)|random32();p.x[10]=DATA+256;
		for(i=0;i<7;i++){
			uint32 q=random32(),rd=5U+(q%5U),rs1=5U+((q>>3)%5U),rs2=5U+((q>>6)%5U);
			if (i == 2U) {
				static const uint8 f3[] = {0U,1U,4U,5U,6U,7U};
				uint32 target = i + 1U + (random32() % (7U - i));
				p.insn[i] = b_insn((sint32)(4U * (target - i)), rs2, rs1, f3[(q >> 9) % 6U]);
				continue;
			}
			switch((q>>9)%16U){
			case 0:p.insn[i]=r_insn(0,rs2,rs1,0,rd,0x33);break;
			case 1:p.insn[i]=r_insn(0x20,rs2,rs1,0,rd,0x33);break;
			case 2:p.insn[i]=r_insn(1,rs2,rs1,0,rd,0x33);break;
			case 3:p.insn[i]=r_insn(0,rs2,rs1,7,rd,0x33);break;
			case 4:p.insn[i]=r_insn(0,rs2,rs1,6,rd,0x33);break;
			case 5:p.insn[i]=r_insn(0,rs2,rs1,4,rd,0x33);break;
			case 6:p.insn[i]=r_insn(0,rs2,rs1,1,rd,0x33);break;
			case 7:p.insn[i]=r_insn(0x20,rs2,rs1,5,rd,0x33);break;
			case 8:p.insn[i]=i_insn((sint32)(q&63U),10,3,rd,0x03);break;
			case 9:p.insn[i]=s_insn((sint32)(q&63U),rs2,10,3);break;
			case 10:p.insn[i]=i_insn((sint32)(q&31U),rs1,1,rd,0x1b);break;
			case 11:p.insn[i]=i_insn((sint32)(q&0x7ffU),rs1,0,rd,0x13);break;
			default:{
				static const uint8 f3[] = {0U,1U,4U,5U,6U,7U};
				uint32 target = i + 1U + (random32() % (7U - i));
				p.insn[i]=b_insn((sint32)(4U*(target-i)),rs2,rs1,f3[((q>>9)-12U)%6U]);
				break;
			}
			}
		}
		p.insn[7]=j_insn(-28,0U);
		steps[0].budget=8U;steps[1].budget=8U;
		for(k=0U;k<10U;k++){steps[k].set_pc=FALSE;steps[k].pc=0U;if(k>=2U)steps[k].budget=1U+(random32()%16U);}
		run_supplies(&p,steps,10U,FALSE,interp);run_supplies(&p,steps,10U,TRUE,jit);
		/* Chaining can build target-entry block metadata earlier; it is non-architectural. */
		for(k=0U;k<10U;k++){
			char supply_name[128];
			snprintf(supply_name,sizeof(supply_name),"%s/supply-%u",name,k);
			compare_arch_without_decode_cache(supply_name,&interp[k],&jit[k]);
		}
		{
			uint64 blocks=0U,native=0U;
			for(k=0U;k<10U;k++){blocks+=jit[k].blocks;native+=jit[k].native;}
			if(blocks==0U)fail_field(name,"random JIT block proof");
			if(native==0U)fail_field(name,"random native-path proof");
		}
		if(failures!=before){fprintf(stderr,"random mismatch seed=0x%08x program=%u\n",RANDOM_SEED,program);return;}
	}
}

int main(void)
{
	uint32 i;
	for(i=0;i<RAM_SIZE;i++) initial_memory[i]=(uint8)(i*37U+11U);
	memcpy(memory,initial_memory,RAM_SIZE);
	virtual_cpu.core_id_num=1;
	riscv_idle_spin_halt_init();
	athrill_idle_spin_halt_init();
	cpu_init();
	alu_cases(); shift_logic_cases(); branch_cases(); jump_cases(); chaining_cases();
	if (failures == 0U) {
		link_drop_cases(); memory_cases(); pmp_cases(); code_store_cases(); early_end_cases(); random_cases();
	}
	printf("JIT differential: seed=0x%08x random-programs=1000 supply-calls=10 %s (%u failures)\n",RANDOM_SEED,failures?"FAIL":"PASS",failures);
	return failures?1:0;
}
