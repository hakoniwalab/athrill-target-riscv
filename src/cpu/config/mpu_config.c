#include "mpu.h"
#include "cpu_config.h"
#include <stdio.h>

#define SERIAL_ADDR 0x10000000U
static uint8 serial_data[4];

static Std_ReturnType get8(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint8 *d) { (void)c; *d=r->data[a-r->start]; return STD_E_OK; }
static Std_ReturnType get16(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint16 *d) { uint8 *p; (void)c; p=&r->data[a-r->start]; *d=(uint16)p[0]|(uint16)p[1]<<8; return STD_E_OK; }
static Std_ReturnType get32(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint32 *d) { uint8 *p; (void)c; p=&r->data[a-r->start]; *d=(uint32)p[0]|(uint32)p[1]<<8|(uint32)p[2]<<16|(uint32)p[3]<<24; return STD_E_OK; }
static Std_ReturnType put8(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint8 d) { (void)c; r->data[a-r->start]=d; if(a==SERIAL_ADDR){putchar(d);fflush(stdout);} return STD_E_OK; }
static Std_ReturnType put16(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint16 d) { put8(r,c,a,(uint8)d); if(a+1<SERIAL_ADDR+4)r->data[a+1-r->start]=(uint8)(d>>8); return STD_E_OK; }
static Std_ReturnType put32(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint32 d) { unsigned i; for(i=0;i<4;i++)r->data[a+i-r->start]=(uint8)(d>>(8*i)); if(a==SERIAL_ADDR){putchar((uint8)d);fflush(stdout);} (void)c; return STD_E_OK; }
static Std_ReturnType ptr(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint8 **d) { (void)c; *d=&r->data[a-r->start]; return STD_E_OK; }
static MpuAddressRegionOperationType serial_ops={get8,get16,get32,put8,put16,put32,ptr};

MpuAddressMapType mpu_address_map={.dynamic_map_num=0,.dynamic_map=NULL,.map={{.type=DEVICE,.is_malloc=FALSE,.permission=MPU_ADDRESS_REGION_PERM_ALL,.start=SERIAL_ADDR,.size=4,.mask=MPU_ADDRESS_REGION_MASK_ALL,.data=serial_data,.ops=&serial_ops}}};
