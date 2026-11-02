#include "cpu.h"
#include "std_device_ops.h"
#include "mpu_ops.h"
#include "std_errno.h"
#include "std_cpu_ops.h"
#include <stdio.h>
#include <string.h>

static uint8 memory[256];
int cpu_config_get_core_id_num(void){return 1;}
Std_ReturnType mpu_get_pointer(CoreIdType id,AthrillAddrType a,uint8**p){(void)id;if(a>=sizeof(memory))return STD_E_INVALID;*p=&memory[a];return STD_E_OK;}
Std_ReturnType mpu_get_data8(CoreIdType id,AthrillAddrType a,uint8*d){uint8*p;if(mpu_get_pointer(id,a,&p)!=STD_E_OK)return STD_E_INVALID;*d=*p;return STD_E_OK;}
Std_ReturnType mpu_get_data16(CoreIdType id,AthrillAddrType a,uint16*d){uint8*p;if(mpu_get_pointer(id,a,&p)!=STD_E_OK)return STD_E_INVALID;*d=(uint16)p[0]|(uint16)p[1]<<8;return STD_E_OK;}
Std_ReturnType mpu_get_data32(CoreIdType id,AthrillAddrType a,uint32*d){uint8*p;if(mpu_get_pointer(id,a,&p)!=STD_E_OK)return STD_E_INVALID;*d=(uint32)p[0]|(uint32)p[1]<<8|(uint32)p[2]<<16|(uint32)p[3]<<24;return STD_E_OK;}
Std_ReturnType mpu_put_data8(CoreIdType id,AthrillAddrType a,uint8 d){uint8*p;if(mpu_get_pointer(id,a,&p)!=STD_E_OK)return STD_E_INVALID;*p=d;return STD_E_OK;}
Std_ReturnType mpu_put_data16(CoreIdType id,AthrillAddrType a,uint16 d){return mpu_put_data8(id,a,(uint8)d)==STD_E_OK?mpu_put_data8(id,a+1,(uint8)(d>>8)):STD_E_INVALID;}
Std_ReturnType mpu_put_data32(CoreIdType id,AthrillAddrType a,uint32 d){unsigned n;for(n=0;n<4;n++)if(mpu_put_data8(id,a+n,(uint8)(d>>(8*n)))!=STD_E_OK)return STD_E_INVALID;return STD_E_OK;}

extern void device_init(CpuType*,DeviceClockType*);
extern void device_supply_clock(DeviceClockType*);
int main(void){DeviceClockType clock;unsigned n;const uint32 addi=0x00108093U,ecall=0x00000073U,ebreak=0x00100073U;memset(memory,0,sizeof(memory));for(n=0;n<10;n++)memcpy(&memory[n*4],&addi,4);memcpy(&memory[40],&ecall,4);memcpy(&memory[44],&ebreak,4);virtual_cpu.core_id_num=1;cpu_init();device_init(&virtual_cpu,&clock);for(n=0;n<10;n++){device_supply_clock(&clock);cpu_supply_clock(0);clock.clock++;}if(clock.clock!=10||virtual_cpu.cores[0].core.reg.csr.mcycle!=10||virtual_cpu.cores[0].core.reg.csr.minstret!=10||virtual_cpu.cores[0].core.reg.x[1]!=10||virtual_cpu.cores[0].elaps!=10){fprintf(stderr,"clock=%llu mcycle=%llu minstret=%llu x1=%llu elaps=%llu\n",clock.clock,virtual_cpu.cores[0].core.reg.csr.mcycle,virtual_cpu.cores[0].core.reg.csr.minstret,virtual_cpu.cores[0].core.reg.x[1],virtual_cpu.cores[0].elaps);return 1;}virtual_cpu.cores[0].core.reg.csr.mtvec=44;cpu_supply_clock(0);if(virtual_cpu.cores[0].core.reg.csr.mcycle!=11||virtual_cpu.cores[0].core.reg.csr.minstret!=10||virtual_cpu.cores[0].core.reg.csr.mcause!=11||virtual_cpu.cores[0].core.reg.csr.mepc!=40)return 2;cpu_supply_clock(0);if(virtual_cpu.cores[0].core.reg.csr.mcause!=3||virtual_cpu.cores[0].core.reg.csr.minstret!=10)return 3;return 0;}
