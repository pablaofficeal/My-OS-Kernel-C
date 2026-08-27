#include "timer.h"

#include "../kernel/klog.h"

#define PIT_INPUT_HZ 1193182U
#define PIT_COMMAND  0x43
#define PIT_CHANNEL0 0x40

static volatile uint64_t tick_count;
static volatile uint64_t idle_tsc;
static uint32_t timer_frequency_hz=1000;

static inline void outb(uint16_t port, uint8_t value){
    __asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port));
}

static inline uint8_t inb(uint16_t port){
    uint8_t value;
    __asm__ volatile("inb %1,%0":"=a"(value):"Nd"(port));
    return value;
}

static inline uint64_t read_tsc(void){
    uint32_t low,high;
    __asm__ volatile("rdtsc":"=a"(low),"=d"(high));
    return ((uint64_t)high<<32)|low;
}

void timer_init(uint32_t frequency_hz){
    if(frequency_hz<100) frequency_hz=100;
    if(frequency_hz>2000) frequency_hz=2000;
    uint32_t divisor=(PIT_INPUT_HZ+frequency_hz/2)/frequency_hz;
    if(divisor<1) divisor=1;
    if(divisor>0xFFFF) divisor=0xFFFF;
    timer_frequency_hz=PIT_INPUT_HZ/divisor;

    outb(PIT_COMMAND,0x36);
    outb(PIT_CHANNEL0,(uint8_t)divisor);
    outb(PIT_CHANNEL0,(uint8_t)(divisor>>8));
    uint8_t mask=inb(0x21);
    outb(0x21,(uint8_t)(mask&~1U));
    klogf(KLOG_OK,"pit: timer ready at %u Hz",timer_frequency_hz);
}

void timer_tick(void){ tick_count++; }

void timer_sleep(uint32_t milliseconds){
    if(!milliseconds) return;
    uint64_t wait_ticks=((uint64_t)milliseconds*timer_frequency_hz+999)/1000;
    if(!wait_ticks) wait_ticks=1;
    uint64_t deadline=tick_count+wait_ticks;
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0":"=r"(flags));
    while((int64_t)(tick_count-deadline)<0){
        uint64_t before=read_tsc();
        __asm__ volatile("sti; hlt":::"memory");
        idle_tsc+=read_tsc()-before;
    }
    if(!(flags&(1ULL<<9))) __asm__ volatile("cli":::"memory");
}

uint64_t timer_ticks(void){ return tick_count; }

uint64_t timer_idle_tsc(void){ return idle_tsc; }
