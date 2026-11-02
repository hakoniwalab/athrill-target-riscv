#include "cpu.h"
#include "mpu_ops.h"
#include "std_cpu_ops.h"
#include "std_device_ops.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint8 memory[64];
int cpu_config_get_core_id_num(void){return 1;}
Std_ReturnType mpu_get_pointer(CoreIdType id,AthrillAddrType a,uint8**p){(void)id;if(a>=sizeof(memory))return STD_E_INVALID;*p=&memory[a];return STD_E_OK;}
Std_ReturnType mpu_get_data8(CoreIdType id,AthrillAddrType a,uint8*d){uint8*p;if(mpu_get_pointer(id,a,&p)!=STD_E_OK)return STD_E_INVALID;*d=*p;return STD_E_OK;}
Std_ReturnType mpu_get_data16(CoreIdType id,AthrillAddrType a,uint16*d){uint8*p;if(mpu_get_pointer(id,a,&p)!=STD_E_OK)return STD_E_INVALID;memcpy(d,p,2);return STD_E_OK;}
Std_ReturnType mpu_get_data32(CoreIdType id,AthrillAddrType a,uint32*d){uint8*p;if(mpu_get_pointer(id,a,&p)!=STD_E_OK)return STD_E_INVALID;memcpy(d,p,4);return STD_E_OK;}
Std_ReturnType mpu_put_data8(CoreIdType id,AthrillAddrType a,uint8 d){uint8*p;if(mpu_get_pointer(id,a,&p)!=STD_E_OK)return STD_E_INVALID;*p=d;return STD_E_OK;}
Std_ReturnType mpu_put_data16(CoreIdType id,AthrillAddrType a,uint16 d){(void)id;(void)a;(void)d;return STD_E_INVALID;}
Std_ReturnType mpu_put_data32(CoreIdType id,AthrillAddrType a,uint32 d){(void)id;(void)a;(void)d;return STD_E_INVALID;}
void device_init(CpuType*c,DeviceClockType*d){(void)c;(void)d;}
void device_reset(CpuType*c,DeviceClockType*d){(void)c;(void)d;}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return(double)t.tv_sec+(double)t.tv_nsec/1e9;}
int main(int argc,char**argv){const uint64 count=30000000ULL;const uint32 code[]={0x00000293U,0x00128293U,0x0552c313U,0xff9ff06fU};uint64 n;double start,end;int cold=argc>1;(void)argv;memcpy(memory,code,sizeof(code));virtual_cpu.core_id_num=1;cpu_init();for(n=0;n<10000;n++)cpu_supply_clock(0);start=now();for(n=0;n<count;n++){if(cold)virtual_cpu.cores[0].core.decode_cache[(virtual_cpu.cores[0].core.reg.pc>>1)&(RISCV_DECODE_CACHE_ENTRIES-1U)].valid=0;cpu_supply_clock(0);}end=now();printf("%s %.3f MIPS\n",cold?"decode-every-time":"cached",(double)count/(end-start)/1e6);return 0;}
