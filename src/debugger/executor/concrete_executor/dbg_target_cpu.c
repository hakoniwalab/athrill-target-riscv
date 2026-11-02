#include "concrete_executor/target/dbg_target_cpu.h"
#include "cpu.h"
#include <inttypes.h>
#include <stdio.h>

void dbg_target_print_cpu(uint32 id)
{
    unsigned i; TargetCoreType *c=&virtual_cpu.cores[id].core;
    printf("***CPU<%u> RV64***\nPC 0x%016" PRIx64 " privilege %u\n",id,c->reg.pc,(unsigned)c->reg.privilege);
    for(i=0;i<32;i++) printf("x%-2u 0x%016" PRIx64 "%c",i,c->reg.x[i],(i%4)==3?'\n':' ');
    printf("mstatus 0x%016" PRIx64 " mtvec 0x%016" PRIx64 " mepc 0x%016" PRIx64 "\nmcause 0x%016" PRIx64 " mtval 0x%016" PRIx64 "\nmcycle 0x%016" PRIx64 " minstret 0x%016" PRIx64 "\n",c->reg.csr.mstatus,c->reg.csr.mtvec,c->reg.csr.mepc,c->reg.csr.mcause,c->reg.csr.mtval,c->reg.csr.mcycle,c->reg.csr.minstret);
}
void cpu_debug_print_mpu_status(uint32 id) { (void)id; }
