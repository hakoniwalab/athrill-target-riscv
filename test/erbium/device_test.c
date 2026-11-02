#include <stdint.h>
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define REG64(a) (*(volatile uint64_t *)(uintptr_t)(a))
#define UART_TX 0x02004008U
#define UART_RX 0x02004010U
#define UART_STATUS 0x02004018U
#define UART_IE 0x02004030U
#define MTIME 0x80f40200U
#define MTIMECMP 0x80f40208U
#define PLIC_ENABLE 0xa0002000U
#define PLIC_THRESHOLD 0xa0200000U
#define PLIC_CLAIM 0xa0200004U
static volatile unsigned timer_count,rx_count;
static void putc(char c){while((REG32(UART_STATUS)&2U)!=0U){}REG32(UART_TX)=(uint32_t)(uint8_t)c;}
static void puts(const char*s){while(*s)putc(*s++);}
static void putnum(unsigned v){putc((char)('0'+v%10U));}
void trap_handler(void) __attribute__((interrupt("machine")));
void trap_handler(void){uint64_t cause;__asm__ volatile("csrr %0, mcause":"=r"(cause));if(cause==((1ULL<<63)|7ULL)){timer_count++;REG64(MTIMECMP)=REG64(MTIME)+100U;puts("timer ");putnum(timer_count);putc('\n');}else if(cause==((1ULL<<63)|11ULL)){uint32_t source=REG32(PLIC_CLAIM);if(source==3U){char c=(char)REG32(UART_RX);puts("echo ");putc(c);putc('\n');rx_count++;}REG32(PLIC_CLAIM)=source;}else{puts("unexpected trap\n");for(;;){}}}
int main(void){puts("erbium device test\n");REG32(PLIC_THRESHOLD)=0;REG32(PLIC_ENABLE)=1U<<3;REG32(UART_IE)=1U<<2;puts("plic armed\n");REG64(MTIMECMP)=REG64(MTIME)+100U;puts("timer armed\n");__asm__ volatile("csrs mie, %0"::"r"((1ULL<<7)|(1ULL<<11)));__asm__ volatile("csrsi mstatus, 8");puts("interrupts on\n");while(timer_count<3U||rx_count<1U)__asm__ volatile("wfi");puts("PASS\n");__asm__ volatile("csrci mstatus, 8");REG64(MTIMECMP)=~0ULL;REG32(UART_IE)=0;for(;;)__asm__ volatile("wfi");}
