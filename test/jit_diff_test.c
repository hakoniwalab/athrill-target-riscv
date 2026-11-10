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
extern void riscv_jit_test_get_counters(uint64 *, uint64 *, uint64 *, uint64 *);

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
		*value = 0U;
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

enum Path { PATH_ANY, PATH_NATIVE, PATH_C };
typedef struct {
	const char *name;
	uint64 pc;
	uint32 insn[MAX_INSNS];
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
	uint8 scenario;
} Plan;

typedef struct {
	CpuRegisterType reg;
	uint64 cycle, instret, clock, done, elaps, decode_hash;
	std_bool access_fault, halt;
	uint8 ram[RAM_SIZE];
	uint64 blocks, native, called, runtime_calls;
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

static void decode_one(TargetCoreType *core, uint64 pc)
{
	RiscvDecodedInsn *e = riscv_decode_slot(core, pc);
	uint32 raw;
	memcpy(&raw, &memory[pc], 4U);
	if ((e == NULL) || !riscv_decode_fill(pc, raw, 4U, e)) {
		fprintf(stderr, "FAIL %s: cannot decode %08x at 0x%" PRIx64 "\n", active_case, raw, pc);
		exit(2);
	}
	e->code = &memory[pc]; e->raw = raw; e->pmp_epoch = core->pmp_epoch; e->privilege = core->reg.privilege;
}
static void decode_plan(TargetCoreType *core, const Plan *p)
{
	uint32 i;
	for (i = 0; i < p->count; i++) decode_one(core, p->pc + 4U * i);
}

static void init_core(TargetCoreType *core, const Plan *p)
{
	uint32 i;

	memcpy(memory, initial_memory, RAM_SIZE);
	for (i = 0U; i < p->count; i++) put32(p->pc + 4U * i, p->insn[i]);
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
	if (p->scenario == 2U) decode_one(core, OTHER_CODE);
}

static Result run(const Plan *p, std_bool jit)
{
	TargetCoreType *core = &virtual_cpu.cores[0].core;
	Result r;
	uint64 b0, n0, c0, rc0;
	uint64 b1, n1, c1, rc1;
	uint64 done = 0U, extra = 0U;

	active_case = p->name;
	init_core(core, p);
	core = &virtual_cpu.cores[0].core;
	riscv_jit_test_set_enabled(jit);
	riscv_jit_test_get_counters(&b0, &n0, &c0, &rc0);
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
	riscv_jit_test_get_counters(&b1, &n1, &c1, &rc1);
	memset(&r, 0, sizeof(r));
	r.reg = core->reg; r.cycle = core->internal_cycle; r.instret = core->internal_instret;
	r.clock = test_clock; r.done = done; r.elaps = virtual_cpu.cores[0].elaps;
	r.access_fault = core->access_fault; r.halt = core->is_halt;
	r.decode_hash = decode_hash(core); memcpy(r.ram, memory, RAM_SIZE);
	r.blocks = b1 - b0; r.native = n1 - n0; r.called = c1 - c0; r.runtime_calls = rc1 - rc0;
	return r;
}

static void fail_field(const char *name, const char *field)
{
	fprintf(stderr, "FAIL %s: interpreter/JIT differ in %s\n", name, field);
	failures++;
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
	if (failures != before) {
		fprintf(stderr, "  JIT counters blocks=%" PRIu64 " native=%" PRIu64 " called=%" PRIu64
			" runtime-calls=%" PRIu64 "\n", b.blocks, b.native, b.called, b.runtime_calls);
	}
}

static Plan basic(const char *name, uint32 instruction, uint64 a, uint64 b, uint32 rd, enum Path path)
{
	Plan p;
	memset(&p, 0, sizeof(p));
	p.name = name; p.pc = CODE; p.insn[0] = instruction; p.insn[1] = i_insn(1, 31, 0, 31, 0x13U);
	p.count = 2U; p.budget = 2U; p.x[5] = a; p.x[7] = b; p.x[31] = 9U;
	p.privilege = RISCV_PRIV_M; p.path = path; p.min_native = 2U; p.expected_done = 2U;
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
	uint32 block,i; char name[64];
	for(block=0;block<2000U;block++){
		Plan p; memset(&p,0,sizeof(p));snprintf(name,sizeof(name),"random/seed-%08x/block-%u",RANDOM_SEED,block);p.name=name;p.pc=CODE;p.count=8;p.budget=8;p.privilege=RISCV_PRIV_M;p.path=PATH_NATIVE;p.min_native=8;p.expected_done=8;
		p.x[5]=((uint64)random32()<<32)|random32();p.x[6]=((uint64)random32()<<32)|random32();p.x[7]=((uint64)random32()<<32)|random32();p.x[8]=((uint64)random32()<<32)|random32();p.x[9]=((uint64)random32()<<32)|random32();p.x[10]=DATA+256;
		for(i=0;i<7;i++){
			uint32 q=random32(),rd=5U+(q%5U),rs1=5U+((q>>3)%5U),rs2=5U+((q>>6)%5U);
			switch((q>>9)%12U){
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
			default:p.insn[i]=i_insn((sint32)(q&0x7ffU),rs1,0,rd,0x13);break;
			}
			put32(CODE+4U*i,p.insn[i]);
		}
		p.insn[7]=i_insn(1,31,0,31,0x13);put32(CODE+28,p.insn[7]);run_pair(&p);
		if(failures){fprintf(stderr,"random seed: 0x%08x\n",RANDOM_SEED);return;}
	}
}

int main(void)
{
	uint32 i;
	for(i=0;i<RAM_SIZE;i++) initial_memory[i]=(uint8)(i*37U+11U);
	memcpy(memory,initial_memory,RAM_SIZE);
	virtual_cpu.core_id_num=1;
	cpu_init();
	alu_cases(); shift_logic_cases(); memory_cases(); pmp_cases(); code_store_cases(); early_end_cases(); random_cases();
	printf("JIT differential: seed=0x%08x random-blocks=2000 %s (%u failures)\n",RANDOM_SEED,failures?"FAIL":"PASS",failures);
	return failures?1:0;
}
