#include "erbium_devices.h"
#include "concrete_executor/target/dbg_target_serial.h"
#include <string.h>
#define UART_BASE 0x02004000ULL
#define UART_FIFO_SIZE 16U
#define UART_STATUS_TX_EMPTY (1U << 0)
#define UART_STATUS_RX_NOT_EMPTY (1U << 2)
uint8 erbium_uart_data[0x100];
static uint32 baud,control,ie,rx_threshold;
static uint8 rx_fifo[UART_FIFO_SIZE];
static unsigned rx_head,rx_count;
static uint64 uart_clock;
static std_bool tx_busy;
static void update_irq(void){erbium_plic_set_source(3,((ie&UART_STATUS_RX_NOT_EMPTY)!=0U&&rx_count>rx_threshold)?TRUE:FALSE);}
Std_ReturnType erbium_uart_receive(uint8 data){unsigned tail;if(rx_count==UART_FIFO_SIZE)return STD_E_LIMIT;tail=(rx_head+rx_count)%UART_FIFO_SIZE;rx_fifo[tail]=data;rx_count++;update_irq();return STD_E_OK;}
static uint32 read_reg(AthrillAddrType a){uint32 v;switch((unsigned)(a-UART_BASE)){case 0:return baud;case 0x10:if(rx_count==0U)return 0U;v=rx_fifo[rx_head];rx_head=(rx_head+1U)%UART_FIFO_SIZE;rx_count--;update_irq();return v;case 0x18:return(!tx_busy?UART_STATUS_TX_EMPTY:0U)|(rx_count!=0U?UART_STATUS_RX_NOT_EMPTY:0U);case 0x28:return control;case 0x30:return ie;case 0x40:return rx_threshold;default:return 0;}}
static void write_reg(AthrillAddrType a,uint32 v){switch((unsigned)(a-UART_BASE)){case 0:baud=v;break;case 8:(void)dbg_serial_putchar(0,(uint8)v);tx_busy=TRUE;break;case 0x28:control=v;break;case 0x30:ie=v;update_irq();break;case 0x40:rx_threshold=v;update_irq();break;default:break;}}
static Std_ReturnType g8(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint8*d){uint32 v=read_reg(a&~3ULL);(void)r;(void)c;*d=(uint8)(v>>((a&3U)*8U));return STD_E_OK;}
static Std_ReturnType g16(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint16*d){uint8 x,y;g8(r,c,a,&x);g8(r,c,a+1,&y);*d=(uint16)x|((uint16)y<<8);return STD_E_OK;}
static Std_ReturnType g32(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint32*d){(void)r;(void)c;*d=read_reg(a);return STD_E_OK;}
static Std_ReturnType p8(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint8 d){(void)r;(void)c;write_reg(a,d);return STD_E_OK;}
static Std_ReturnType p16(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint16 d){(void)r;(void)c;write_reg(a,d);return STD_E_OK;}
static Std_ReturnType p32(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint32 d){(void)r;(void)c;write_reg(a,d);return STD_E_OK;}
MpuAddressRegionOperationType erbium_uart_ops={g8,g16,g32,p8,p16,p32,NULL};
void erbium_uart_reset(void){memset(erbium_uart_data,0,sizeof(erbium_uart_data));memset(rx_fifo,0,sizeof(rx_fifo));baud=control=ie=rx_threshold=0;rx_head=rx_count=0;uart_clock=0;tx_busy=FALSE;update_irq();}
void erbium_uart_tick(void){uart_clock++;if((uart_clock%30U)==0U)tx_busy=FALSE;}
