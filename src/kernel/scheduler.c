#include "scheduler.h"
#include "../drivers/timer.h"
#include "../kernel/klog.h"
#include "../kernel/panic.h"
#include "../arch/x86_64/gdt.h"
#include "../boot/limine.h"
#include "../lib/string.h"

extern struct limine_smp_response *smp_response_ptr;

static struct thread threads[SCHEDULER_MAX_THREADS];
static struct thread *current = NULL;
static uint32_t next_id = 1;
static bool initialized = false;
static volatile bool need_resched = false;
static uint32_t core_count = 1;
static void thread_trampoline(void);
static struct thread *pick_next(void);

extern void scheduler_asm_switch(uint64_t *old_rsp, uint64_t *new_rsp);

static void thread_trampoline(void){
    // This runs as new thread's first execution after ret
    // Enable interrupts for thread
    __asm__ volatile("sti":::"memory");
    struct thread *self = current;
    if(self && self->entry){
        klogf(KLOG_DEBUG, "sched: thread %u (%s) started on core %u", self->id, self->name, 0);
        self->entry(self->arg);
    }
    scheduler_exit();
    for(;;) __asm__ volatile("hlt");
}

void scheduler_init(void){
    if(initialized) return;
    memset(threads, 0, sizeof(threads));
    if(smp_response_ptr && smp_response_ptr->cpu_count>0){
        core_count = (uint32_t)smp_response_ptr->cpu_count;
        if(core_count>8) core_count=8;
        if(core_count==0) core_count=1;
    } else {
        core_count = 1;
    }
    struct thread *idle = &threads[0];
    idle->id = 0;
    idle->state = THREAD_RUNNING;
    idle->priority = 7;
    idle->affinity = -1;
    idle->ticks_remaining = SCHEDULER_TIME_SLICE_MS;
    strncpy(idle->name, "idle", sizeof(idle->name)-1);
    idle->entry = NULL;
    current = idle;
    initialized = true;
    klogf(KLOG_OK, "sched: initialized, cores=%u max_threads=%u stack=%u", core_count, SCHEDULER_MAX_THREADS, SCHEDULER_STACK_SIZE);
}

static struct thread *alloc_thread(void){
    for(int i=1;i<SCHEDULER_MAX_THREADS;i++){
        if(threads[i].state==THREAD_FREE){
            return &threads[i];
        }
    }
    return NULL;
}

int scheduler_create_thread(void (*entry)(void *arg), void *arg, const char *name, uint8_t priority, int16_t affinity){
    if(!initialized) return -1;
    if(!entry) return -1;
    if(priority>7) priority=7;
    struct thread *t = alloc_thread();
    if(!t) return -1;
    memset(t, 0, sizeof(*t));
    t->id = next_id++;
    t->entry = entry;
    t->arg = arg;
    t->state = THREAD_READY;
    t->priority = priority;
    t->affinity = affinity;
    t->ticks_remaining = SCHEDULER_TIME_SLICE_MS;
    if(name) strncpy(t->name, name, sizeof(t->name)-1);
    else strncpy(t->name, "thread", sizeof(t->name)-1);

    uint64_t stack_top = (uint64_t)(t->stack + SCHEDULER_STACK_SIZE);
    stack_top &= ~0xFULL;
    uint64_t *stack_ptr = (uint64_t*)stack_top;
    *--stack_ptr = (uint64_t)thread_trampoline;
    *--stack_ptr = 0;
    *--stack_ptr = 0;
    *--stack_ptr = 0;
    *--stack_ptr = 0;
    *--stack_ptr = 0;
    *--stack_ptr = 0;
    t->rsp = (uint64_t)stack_ptr;

    if(affinity>=0 && (uint32_t)affinity>=core_count){
        klogf(KLOG_WARN, "sched: thread %u affinity %d exceeds core count %u, using any", t->id, affinity, core_count);
        t->affinity = -1;
    }

    klogf(KLOG_INFO, "sched: created thread %u (%s) prio=%u affinity=%d entry=%p arg=%p stack=%p rsp=0x%llx",
          t->id, t->name, t->priority, t->affinity, entry, arg, t->stack, t->rsp);
    return t->id;
}

struct thread *scheduler_current_thread(void){ return current; }
int scheduler_current_tid(void){ return current ? (int)current->id : -1; }
uint32_t scheduler_thread_count(void){
    uint32_t cnt=0;
    for(int i=0;i<SCHEDULER_MAX_THREADS;i++) if(threads[i].state!=THREAD_FREE) cnt++;
    return cnt;
}
void scheduler_set_affinity(int tid, int16_t core){
    for(int i=0;i<SCHEDULER_MAX_THREADS;i++) if(threads[i].id==(uint32_t)tid){
        if(core>=0 && (uint32_t)core>=core_count) return;
        threads[i].affinity=core;
        klogf(KLOG_INFO, "sched: thread %u affinity -> %d", tid, core);
        return;
    }
}
int scheduler_get_core_count(void){ return (int)core_count; }

static struct thread *pick_next(void){
    if(!current) return NULL;
    int start = -1;
    for(int i=0;i<SCHEDULER_MAX_THREADS;i++) if(&threads[i]==current) { start=i; break; }
    if(start<0) start=0;
    for(int prio=0;prio<8;prio++){
        for(int iter=1; iter<SCHEDULER_MAX_THREADS; iter++){
            int idx = (start+iter)%SCHEDULER_MAX_THREADS;
            if(threads[idx].state==THREAD_READY){
                if(threads[idx].priority==prio) return &threads[idx];
            }
        }
    }
    for(int iter=1; iter<SCHEDULER_MAX_THREADS; iter++){
        int idx = (start+iter)%SCHEDULER_MAX_THREADS;
        if(threads[idx].state==THREAD_READY) return &threads[idx];
    }
    if(current->state==THREAD_RUNNING) return current;
    if(threads[0].state!=THREAD_FREE) return &threads[0];
    return NULL;
}

void scheduler_yield(void){
    if(!initialized) return;
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
    struct thread *prev = current;
    struct thread *next = pick_next();
    if(!next || next==prev){
        if(flags & (1ULL<<9)) __asm__ volatile("sti":::"memory");
        return;
    }
    if(prev->state==THREAD_RUNNING) prev->state=THREAD_READY;
    next->state=THREAD_RUNNING;
    current=next;
    prev->ticks_remaining = SCHEDULER_TIME_SLICE_MS;
    next->ticks_remaining = SCHEDULER_TIME_SLICE_MS;
    klogf(KLOG_DEBUG, "sched: yield %u (%s) -> %u (%s)", prev->id, prev->name, next->id, next->name);
    scheduler_asm_switch(&prev->rsp, &next->rsp);
    if(flags & (1ULL<<9)) __asm__ volatile("sti":::"memory");
}

void scheduler_block(void){
    if(!current) return;
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
    current->state = THREAD_BLOCKED;
    klogf(KLOG_DEBUG, "sched: thread %u (%s) blocked", current->id, current->name);
    struct thread *prev = current;
    struct thread *next = pick_next();
    if(!next){
        klog(KLOG_ERROR, "sched: no thread to schedule after block!");
        for(;;) __asm__ volatile("cli; hlt");
    }
    next->state = THREAD_RUNNING;
    current = next;
    scheduler_asm_switch(&prev->rsp, &next->rsp);
    if(flags & (1ULL<<9)) __asm__ volatile("sti":::"memory");
}

void scheduler_unblock(int tid){
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
    for(int i=0;i<SCHEDULER_MAX_THREADS;i++) if(threads[i].id==(uint32_t)tid){
        if(threads[i].state==THREAD_BLOCKED){
            threads[i].state=THREAD_READY;
            klogf(KLOG_DEBUG, "sched: thread %u unblocked", tid);
        }
        break;
    }
    if(flags & (1ULL<<9)) __asm__ volatile("sti":::"memory");
}

void scheduler_exit(void){
    if(!current) for(;;) __asm__ volatile("cli; hlt");
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
    klogf(KLOG_INFO, "sched: thread %u (%s) exiting", current->id, current->name);
    current->state = THREAD_TERMINATED;
    struct thread *prev = current;
    struct thread *next = pick_next();
    if(!next || next==prev){
        klog(KLOG_WARN, "sched: last thread exiting, halting");
        for(;;) __asm__ volatile("cli; hlt");
    }
    next->state = THREAD_RUNNING;
    current = next;
    scheduler_asm_switch(&prev->rsp, &next->rsp);
    if(flags & (1ULL<<9)) __asm__ volatile("sti":::"memory");
    for(;;) __asm__ volatile("hlt");
}

void scheduler_tick(void){
    if(!initialized || !current) return;
    if(current->ticks_remaining>0) current->ticks_remaining--;
    if(current->ticks_remaining==0){
        need_resched = true;
    }
    uint64_t now = timer_ticks();
    for(int i=0;i<SCHEDULER_MAX_THREADS;i++){
        if(threads[i].state==THREAD_BLOCKED && threads[i].wake_tick!=0 && now>=threads[i].wake_tick){
            threads[i].state=THREAD_READY;
            threads[i].wake_tick=0;
        }
    }
}

void scheduler_schedule(void){
    if(!need_resched) return;
    need_resched=false;
    scheduler_yield();
}

void scheduler_start(void){
    if(!initialized) return;
    klogf(KLOG_INFO, "sched: starting with %u threads", scheduler_thread_count());
    scheduler_yield();
}
