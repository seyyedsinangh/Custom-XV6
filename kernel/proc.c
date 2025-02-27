#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include <stddef.h>
#include "child_processes.h"
#include "report_traps.h"
#include "queue.h"
#include "priority_queue.h"

struct report_list _internal_report_list = {.numberOfReports=0,.writeIndex=0};

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct PriorityQueue pq;

struct Queue q;

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

const char *threadstate[] = { "THREAD_FREE", "THREAD_RUNNABLE", "THREAD_RUNNING", "THREAD_JOINED" };

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
    struct proc *p;

    for(p = proc; p < &proc[NPROC]; p++) {
        char *pa = kalloc();
        if(pa == 0)
            panic("kalloc");
        uint64 va = KSTACK((int) (p - proc));
        kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
    }
}

// initialize the proc table.
void
procinit(void)
{
    struct proc *p;
    struct thread *t;

    initlock(&pid_lock, "nextpid");
    initlock(&wait_lock, "wait_lock");
    for(p = proc; p < &proc[NPROC]; p++) {
        initlock(&p->lock, "proc");
        p->state = UNUSED;
        p->kstack = KSTACK((int) (p - proc));
        for (t = p->threads; t < &p->threads[MAX_THREAD]; t++) {
            t->state = THREAD_FREE;
        }
    }

    pq_init(&pq);
    initializeQueue(&q);
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
    int id = r_tp();
    return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
    int id = cpuid();
    struct cpu *c = &cpus[id];
    return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
    push_off();
    struct cpu *c = mycpu();
    struct proc *p = c->proc;
    pop_off();
    return p;
}

int
allocpid()
{
    int pid;

    acquire(&pid_lock);
    pid = nextpid;
    nextpid = nextpid + 1;
    release(&pid_lock);

    return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
    struct proc *p;
    for(p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if(p->state == UNUSED) {
            goto found;
        } else {
            release(&p->lock);
        }
    }
    return 0;

    found:
    p->pid = allocpid();
    p->state = USED;

    // Allocate a trapframe page.
    if((p->trapframe = (struct trapframe *)kalloc()) == 0){
        freeproc(p);
        release(&p->lock);
        return 0;
    }

    // Allocate a cpu_usage struct.
    if((p->usage_time = (struct cpu_usage *)kalloc()) == 0){
        freeproc(p);
        release(&p->lock);
        return 0;
    }
    p->usage_time->start_tick = 0;
    p->usage_time->sum_of_ticks = 0;
    p->usage_time->quota = MAX_UINT;
    p->usage_time->last_sched_tick = 0;
    p->usage_time->deadline = MAX_UINT;


    // An empty user page table.
    p->pagetable = proc_pagetable(p);
    if(p->pagetable == 0){
        freeproc(p);
        release(&p->lock);
        return 0;
    }

    // Set up new context to start executing at forkret,
    // which returns to user space.
    memset(&p->context, 0, sizeof(p->context));
    p->context.ra = (uint64)forkret;
    p->context.sp = p->kstack + PGSIZE;
    p->current_thread = NULL;
    return p;
}

void freethread(struct thread *t) {
    if(t->trapframe)
        kfree((void*)t->trapframe);
    t->trapframe = 0;
    t->state = THREAD_FREE;
    t->id = 0;
    t->join = 0;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
    if(p->trapframe)
        kfree((void*)p->trapframe);
    p->trapframe = 0;
    if(p->usage_time)
        kfree((void*)p->usage_time);
    p->usage_time = 0;
    if(p->pagetable)
        proc_freepagetable(p->pagetable, p->sz);
    p->pagetable = 0;
    p->sz = 0;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->chan = 0;
    p->killed = 0;
    p->xstate = 0;
    p->state = UNUSED;
    p->current_thread = NULL;
    struct thread *t;
    for (t = p->threads; t < &p->threads[MAX_THREAD]; t++) {
        freethread(t);
    }
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
    pagetable_t pagetable;

    // An empty page table.
    pagetable = uvmcreate();
    if(pagetable == 0)
        return 0;

    // map the trampoline code (for system call return)
    // at the highest user virtual address.
    // only the supervisor uses it, on the way
    // to/from user space, so not PTE_U.
    if(mappages(pagetable, TRAMPOLINE, PGSIZE,
                (uint64)trampoline, PTE_R | PTE_X) < 0){
        uvmfree(pagetable, 0);
        return 0;
    }

    // map the trapframe page just below the trampoline page, for
    // trampoline.S.
    if(mappages(pagetable, TRAPFRAME, PGSIZE,
                (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmfree(pagetable, 0);
        return 0;
    }

    return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
        0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
        0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
        0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
        0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
        0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
        0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
    struct proc *p;

    p = allocproc();
    initproc = p;

    // allocate one user page and copy initcode's instructions
    // and data into it.
    uvmfirst(p->pagetable, initcode, sizeof(initcode));
    p->sz = PGSIZE;

    // prepare for the very first "return" from kernel to user.
    p->trapframe->epc = 0;      // user program counter
    p->trapframe->sp = PGSIZE;  // user stack pointer

    safestrcpy(p->name, "initcode", sizeof(p->name));
    p->cwd = namei("/");

    p->state = RUNNABLE;

    release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
    uint64 sz;
    struct proc *p = myproc();

    sz = p->sz;
    if(n > 0){
        if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
            return -1;
        }
    } else if(n < 0){
        sz = uvmdealloc(p->pagetable, sz, sz + n);
    }
    p->sz = sz;
    return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
    int i, pid;
    struct proc *np;
    struct proc *p = myproc();

    // Allocate process.
    if((np = allocproc()) == 0){
        return -1;
    }

    // Copy user memory from parent to child.
    if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
        freeproc(np);
        release(&np->lock);
        return -1;
    }
    np->sz = p->sz;

    // copy saved user registers.
    *(np->trapframe) = *(p->trapframe);

    // Cause fork to return 0 in the child.
    np->trapframe->a0 = 0;

    // increment reference counts on open file descriptors.
    for(i = 0; i < NOFILE; i++)
        if(p->ofile[i])
            np->ofile[i] = filedup(p->ofile[i]);
    np->cwd = idup(p->cwd);

    safestrcpy(np->name, p->name, sizeof(p->name));

    pid = np->pid;

    release(&np->lock);

    acquire(&wait_lock);
    np->parent = p;
    release(&wait_lock);

    acquire(&np->lock);
    np->state = RUNNABLE;
    release(&np->lock);

    return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
    struct proc *pp;

    for(pp = proc; pp < &proc[NPROC]; pp++){
        if(pp->parent == p){
            pp->parent = initproc;
            wakeup(initproc);
        }
    }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
    struct proc *p = myproc();

    if(p == initproc)
        panic("init exiting");

    // Close all open files.
    for(int fd = 0; fd < NOFILE; fd++){
        if(p->ofile[fd]){
            struct file *f = p->ofile[fd];
            fileclose(f);
            p->ofile[fd] = 0;
        }
    }

    begin_op();
    iput(p->cwd);
    end_op();
    p->cwd = 0;

    acquire(&wait_lock);

    // Give any children to init.
    reparent(p);

    // Parent might be sleeping in wait().
    wakeup(p->parent);

    acquire(&p->lock);

    p->xstate = status;
    p->state = ZOMBIE;

    release(&wait_lock);

    // Jump into the scheduler, never to return.
    sched();
    panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
    struct proc *pp;
    int havekids, pid;
    struct proc *p = myproc();

    acquire(&wait_lock);

    for(;;){
        // Scan through table looking for exited children.
        havekids = 0;
        for(pp = proc; pp < &proc[NPROC]; pp++){
            if(pp->parent == p){
                // make sure the child isn't still in exit() or swtch().
                acquire(&pp->lock);

                havekids = 1;
                if(pp->state == ZOMBIE){
                    // Found one.
                    pid = pp->pid;
                    if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                            sizeof(pp->xstate)) < 0) {
                        release(&pp->lock);
                        release(&wait_lock);
                        return -1;
                    }
                    freeproc(pp);
                    release(&pp->lock);
                    release(&wait_lock);
                    return pid;
                }
                release(&pp->lock);
            }
        }

        // No point waiting if we don't have any children.
        if(!havekids || killed(p)){
            release(&wait_lock);
            return -1;
        }

        // Wait for a child to exit.
        sleep(p, &wait_lock);  //DOC: wait-sleep
    }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
//void
//scheduler(void)
//{
//    struct proc *p;
//    struct cpu *c = mycpu();
//    struct thread *t;
//
//    c->proc = 0;
//
//    for(;;){
//        // The most recent process to run may have had interrupts
//        // turned off; enable them to avoid a deadlock if all
//        // processes are waiting.
//        intr_on();
//
//        int found = 0;
//        for(p = proc; p < &proc[NPROC]; p++) {
//            acquire(&p->lock);
//            if(p->state == RUNNABLE) {
//                // Switch to chosen process.  It is the process's job
//                // to release its lock and then reacquire it
//                // before jumping back to us.
//                p->state = RUNNING;
//                c->proc = p;
//                if(p->current_thread != NULL) {
//                    if(p->current_thread->state != THREAD_FREE) {
//                        *(p->current_thread->trapframe) = *(p->trapframe);
//                    }
//                    for (t = p->threads; t < &p->threads[MAX_THREAD]; t++) {
//                        p->state = RUNNING;
//                        c->proc = p;
//
//                        if (t->state == THREAD_RUNNABLE) {
//                            t->state = THREAD_RUNNING;
//                            *(p->trapframe) = *(t->trapframe);
//                            p->current_thread = t;
//
//                            if (p->usage_time->start_tick == 0) p->usage_time->start_tick = ticks;
//                            p->usage_time->last_sched_tick = ticks;
//
//                            swtch(&c->context, &p->context);
//
//                            uint cpu_ticks = ticks - p->usage_time->last_sched_tick;
//                            p->usage_time->sum_of_ticks += cpu_ticks;
//
//                            if (p->state == ZOMBIE || p->state == UNUSED) {
//                                break;
//                            }
//                            if (t->state == THREAD_RUNNING) {
//                                t->state = THREAD_RUNNABLE;
//                            }
//                            if (t->state != THREAD_FREE) {
//                                *(t->trapframe) = *(p->trapframe);
//                            }
//                        }
//                    }
//                } else {
//                    if (p->usage_time->start_tick == 0) p->usage_time->start_tick = ticks;
//                    p->usage_time->last_sched_tick = ticks;
//
//                    swtch(&c->context, &p->context);
//                    uint cpu_ticks = ticks - p->usage_time->last_sched_tick;
//                    p->usage_time->sum_of_ticks += cpu_ticks;
//
//                }
//
//                // Process is done running for now.
//                // It should have changed its p->state before coming back.
//                if (p->state == RUNNING) {
//                    p->state = RUNNABLE;
//                }
//                c->proc = 0;
//                found = 1;
//            }
//            release(&p->lock);
//        }
//        if(found == 0) {
//            // nothing to run; stop running on this core until an interrupt.
//            intr_on();
//            asm volatile("wfi");
//        }
//    }
//}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
    struct proc *p;
    struct cpu *c = mycpu();
    struct thread *t;

    c->proc = 0;
    struct PriorityQueue *pointer_pq = &pq;
    struct Queue *pointer_q = &q;
    for(;;) {
        // The most recent process to run may have had interrupts
        // turned off; enable them to avoid a deadlock if all
        // processes are waiting.
        intr_on();

        int found = 0;
        struct proc *chosen_p = NULL;
        for (p = proc; p < &proc[NPROC]; p++) {
            acquire(&pointer_pq->lock);
            acquire(&p->lock);
            pq_check_and_push(pointer_pq, p);
            release(&p->lock);
            release(&pointer_pq->lock);
        }
        int flag = 0;
        while (flag == 0) {
            acquire(&pointer_pq->lock);
            chosen_p = pq_pop(pointer_pq);
            if (chosen_p == NULL) {
                release(&pointer_pq->lock);
                break;
            }
            acquire(&chosen_p->lock);
            if (ticks >= chosen_p->usage_time->deadline) chosen_p->state = DROPPED;
            else flag = 1;
            release(&chosen_p->lock);
            release(&pointer_pq->lock);
        }
        if (chosen_p == NULL) {
            acquire(&pointer_q->lock);
            chosen_p = pop(pointer_q);
            release(&pointer_q->lock);
        }
        if (chosen_p != NULL) {
            // Switch to chosen process.  It is the process's job
            // to release its lock and then reacquire it
            // before jumping back to us.
            p = chosen_p;
            acquire(&p->lock);
            p->state = RUNNING;
            c->proc = p;
            if (p->current_thread != NULL) {
                if (p->current_thread->state != THREAD_FREE) {
                    *(p->current_thread->trapframe) = *(p->trapframe);
                }

                for (t = p->threads; t < &p->threads[MAX_THREAD]; t++) {
                    if (t->state == THREAD_RUNNABLE) {
                        t->state = THREAD_RUNNING;
                        *(p->trapframe) = *(t->trapframe);
                        p->current_thread = t;

                        if (p->usage_time->start_tick == 0) p->usage_time->start_tick = ticks;
                        p->usage_time->last_sched_tick = ticks;

                        swtch(&c->context, &p->context);

                        uint cpu_ticks = ticks - p->usage_time->last_sched_tick;
                        p->usage_time->sum_of_ticks += cpu_ticks;
                        if (ticks >= p->usage_time->deadline &&
                            (p->state == RUNNABLE || p->state == RUNNABLE)) {
                            p->state = DROPPED;
                        } else if (p->usage_time->sum_of_ticks >= p->usage_time->quota &&
                            (p->state == RUNNABLE || p->state == RUNNABLE)) {
                            p->state = PASSED_QUOTA;
                            acquire(&pointer_q->lock);
                            enqueue(pointer_q, p);
                            release(&pointer_q->lock);
                        }

                        if (p->state == ZOMBIE || p->state == UNUSED) {
                            break;
                        }
                        if (t->state == THREAD_RUNNING) {
                            t->state = THREAD_RUNNABLE;
                        }

                        if (t->state != THREAD_FREE) {
                            *(t->trapframe) = *(p->trapframe);
                        }
                    }
                }
            } else {
                if (p->usage_time->start_tick == 0) p->usage_time->start_tick = ticks;
                p->usage_time->last_sched_tick = ticks;

                swtch(&c->context, &p->context);

                uint cpu_ticks = ticks - p->usage_time->last_sched_tick;
                p->usage_time->sum_of_ticks += cpu_ticks;

                if (ticks >= p->usage_time->deadline &&
                    (p->state == RUNNABLE || p->state == RUNNABLE)) {
                    p->state = DROPPED;
                } else if (p->usage_time->sum_of_ticks >= p->usage_time->quota &&
                    (p->state == RUNNABLE || p->state == RUNNABLE)) {
                    p->state = PASSED_QUOTA;
                    acquire(&pointer_q->lock);
                    enqueue(pointer_q, p);
                    release(&pointer_q->lock);
                }
            }

            if (p->state == RUNNING) {
                p->state = RUNNABLE;
            }

            c->proc = 0;
            found = 1;
            release(&p->lock);
        }

        if(found == 0) {
            // nothing to run; stop running on this core until an interrupt.
            intr_on();
            asm volatile("wfi");
        }
    }
}


// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
    int intena;
    struct proc *p = myproc();

    if(!holding(&p->lock))
        panic("sched p->lock");
    if(mycpu()->noff != 1)
        panic("sched locks");
    if(p->state == RUNNING)
        panic("sched running");
    if(intr_get())
        panic("sched interruptible");

    intena = mycpu()->intena;

    p->usage_time->sum_of_ticks += ticks - p->usage_time->last_sched_tick;
    p->usage_time->last_sched_tick = ticks;

    swtch(&p->context, &mycpu()->context);
    mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
    struct proc *p = myproc();
    acquire(&p->lock);
    p->state = RUNNABLE;
    sched();
    release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
    static int first = 1;

    // Still holding p->lock from scheduler.
    release(&myproc()->lock);

    if (first) {
        // File system initialization must be run in the context of a
        // regular process (e.g., because it calls sleep), and thus cannot
        // be run from main().
        fsinit(ROOTDEV);

        first = 0;
        // ensure other cores see first=0.
        __sync_synchronize();
    }

    usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
    struct proc *p = myproc();

    // Must acquire p->lock in order to
    // change p->state and then call sched.
    // Once we hold p->lock, we can be
    // guaranteed that we won't miss any wakeup
    // (wakeup locks p->lock),
    // so it's okay to release lk.

    acquire(&p->lock);  //DOC: sleeplock1
    release(lk);

    // Go to sleep.
    p->chan = chan;
    p->state = SLEEPING;

    sched();

    // Tidy up.
    p->chan = 0;

    // Reacquire original lock.
    release(&p->lock);
    acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
    struct proc *p;

    for(p = proc; p < &proc[NPROC]; p++) {
        if(p != myproc()){
            acquire(&p->lock);
            if(p->state == SLEEPING && p->chan == chan) {
                p->state = RUNNABLE;
            }
            release(&p->lock);
        }
    }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
    struct proc *p;

    for(p = proc; p < &proc[NPROC]; p++){
        acquire(&p->lock);
        if(p->pid == pid){
            p->killed = 1;
            if(p->state == SLEEPING){
                // Wake process from sleep().
                p->state = RUNNABLE;
            }
            release(&p->lock);
            return 0;
        }
        release(&p->lock);
    }
    return -1;
}

void
setkilled(struct proc *p)
{
    acquire(&p->lock);
    p->killed = 1;
    release(&p->lock);
}

int
killed(struct proc *p)
{
    int k;

    acquire(&p->lock);
    k = p->killed;
    release(&p->lock);
    return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
    struct proc *p = myproc();
    if(user_dst){
        return copyout(p->pagetable, dst, src, len);
    } else {
        memmove((char *)dst, src, len);
        return 0;
    }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
    struct proc *p = myproc();
    if(user_src){
        return copyin(p->pagetable, dst, src, len);
    } else {
        memmove(dst, (char*)src, len);
        return 0;
    }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
    static char *states[] = {
            [UNUSED]    "unused",
            [USED]      "used",
            [SLEEPING]  "sleep ",
            [RUNNABLE]  "runble",
            [RUNNING]   "run   ",
            [ZOMBIE]    "zombie"
    };
    struct proc *p;
    char *state;

    printf("\n");
    for(p = proc; p < &proc[NPROC]; p++){
        if(p->state == UNUSED)
            continue;
        if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
            state = states[p->state];
        else
            state = "???";
        printf("%d %s %s", p->pid, state, p->name);
        printf("\n");
    }
}

int is_father(int father_id, struct proc *p){
    acquire(&p->lock);
    int result = (father_id == p->pid);
    int flag = (p->pid == 1);
    release(&p->lock);
    if (result || flag) return result;
    else {
        acquire(&wait_lock);
        p = p->parent;
        release(&wait_lock);
        return is_father(father_id, p);
    }
}

int child_processes(struct child_processes *cps) {
    cps->count = 0;
    struct proc *p = myproc();
    acquire(&p->lock);
    int father_pid = p->pid;
    release(&p->lock);
    for(p = proc; p < &proc[NPROC]; p++){
        if(p->state == UNUSED) continue;
        if(is_father(father_pid, p)) {
            acquire(&p->lock);
            int index = cps->count;
            cps->processes[index].pid = p->pid;
            cps->processes[index].state = p->state;
            release(&p->lock);
            strncpy(cps->processes[index].name,p->name,16);
            acquire(&wait_lock);
            struct proc *father = p->parent;
            release(&wait_lock);
            acquire(&father->lock);
            cps->processes[index].ppid = father->pid;
            release(&father->lock);
            cps->count++;
        }
    }
    return 0;
}

int report_traps(struct report_traps *reptraps) {
    reptraps->count = 0;
    struct proc *p = myproc();
    acquire(&p->lock);
    int father_pid = p->pid;
    release(&p->lock);
    for (int i=0; i <_internal_report_list.numberOfReports; i++) {
        struct report rp = _internal_report_list.reports[i];
        for (int j=0; j<rp.parents_count; j++) {
            if (rp.parents[j]==father_pid) {
                reptraps->reports[reptraps->count] = rp;
                reptraps->count++;
                break;
            }
        }
        if (reptraps->count==MAX_REPORT_BUFFER_SIZE) break;
    }
    return 0;
}

int create_thread(void (*start_routine) (void*), void *arg, void *pstack) {
    struct proc *p = myproc();
    struct thread *t;
    int flag = 0;
    if(p->current_thread == NULL) {
        t = p->threads;
        t->trapframe = (struct trapframe *)kalloc();
        if (t->trapframe == 0) {
            return -1;
        }
        memset(t->trapframe, 0, sizeof(struct trapframe));
        *(t->trapframe) = *(p->trapframe);
        t->id = 1;
        t->join = 0;
        t->state = THREAD_RUNNABLE;
        p->current_thread = t;
        flag = 1;
    }
    for (t = p->threads; t < &p->threads[MAX_THREAD]; t++) {
        if (t->state == THREAD_FREE) {
            break;
        }
    }
    if (t == &p->threads[MAX_THREAD]) {
        return -1;
    }
    t->trapframe = (struct trapframe *)kalloc();
    if (t->trapframe == 0) {
        if(flag == 1) {
            freethread(p->current_thread);
        }
        return -1;
    }
    memset(t->trapframe, 0, sizeof(struct trapframe));
    *(t->trapframe) = *(p->trapframe);
    t->trapframe->epc = (uint64)start_routine;
    t->trapframe->sp = (uint64) pstack;
    t->trapframe->a0 = (uint64)arg;
    t->trapframe->ra = (uint64)(-1);
    t->id = t - p->threads + 1;
    t->state = THREAD_RUNNABLE;
    return t->id;
}


int
join_thread(int tid)
{
    struct proc *p = myproc();
    struct thread *caller = p->current_thread;
    struct thread *target = 0;

    for (int i = 0; i < MAX_THREAD; i++) {
        if (p->threads[i].id == tid && p->threads[i].state != THREAD_FREE) {
            target = &p->threads[i];
            break;
        }
    }

    if (!target || target->state == THREAD_FREE) {
        return -1;
    }
    caller->join = tid;
    caller->state = THREAD_JOINED;
    yield();
    return 0;
}

int exit_t(int tid, struct proc *p){
    // signal any thread waiting for this thread
    struct thread *target = 0;
    for (int i = 0; i < MAX_THREAD; i++) {
        if (p->threads[i].join == tid) {
            // found the thread waiting for this one
            p->threads[i].state = THREAD_RUNNABLE;
            p->threads[i].join = 0;
        }
        if (p->threads[i].id == tid) {
            target = &p->threads[i];
        }
    }
    if(target == 0 || target->state == THREAD_FREE){
        return -1;
    }
    freethread(target);
    printf("This is ended thread: (tid:%d,pid:%d)\n", tid, p->pid);
    yield();
    return 0;
}

int
exit_thread(int tid)
{
    struct proc *p = myproc();
    struct thread *t = p->current_thread;
    if (tid == -1) {
        return exit_t(t->id,p);
    } else {
        return exit_t(tid, p);
    }
}

int cpu_used() {
    struct proc *p = myproc();
    printf("(cpu_used) pid:%d -> quota:%d\n", p->pid, p->usage_time->quota);
    return (int) p->usage_time->sum_of_ticks;
}

// 0 success, -1 pid is not child of running process, -2 pid not found or not used
int set_cpu_quota(int pid, int quota) {
    struct proc *p = myproc();
    acquire(&p->lock);
    //int father_pid = p->pid;
    release(&p->lock);
    for(p = proc; p < &proc[NPROC]; p++){
        if(p->state == UNUSED) continue;
        acquire(&p->lock);
        int pid_to_check = p->pid;
        release(&p->lock);
        // checking if given pid is the child of running process
//        printf("pid_to_check(%d) == pid(%d)\n",pid_to_check,pid);
        if (pid_to_check == pid) {
            if (pid == p->pid) {
                p->usage_time->quota = (uint) quota;
//                printf("(proc.c - set_quota) quota:%d\n", p->usage_time->quota);
                return 0;
            }
            return -1;
        }
    }
    return -2;
}

int
fork_deadline(int deadline)
{
    int i, pid;
    struct proc *np;
    struct proc *p = myproc();

    // Allocate process.
    if((np = allocproc()) == 0){
        return -1;
    }

    // Copy user memory from parent to child.
    if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
        freeproc(np);
        release(&np->lock);
        return -1;
    }
    np->sz = p->sz;

    // copy saved user registers.
    *(np->trapframe) = *(p->trapframe);

    // Cause fork to return 0 in the child.
    np->trapframe->a0 = 0;

    // increment reference counts on open file descriptors.
    for(i = 0; i < NOFILE; i++)
        if(p->ofile[i])
            np->ofile[i] = filedup(p->ofile[i]);
    np->cwd = idup(p->cwd);

    safestrcpy(np->name, p->name, sizeof(p->name));

    pid = np->pid;

    release(&np->lock);

    acquire(&wait_lock);
    np->parent = p;
    release(&wait_lock);

    acquire(&np->lock);
    np->state = RUNNABLE;
    np->usage_time->deadline = (uint) deadline;
    release(&np->lock);

    return pid;
}


int
top_func(struct top* top_struct){
//    printf("top_func! \n");
    struct proc *p;
//    printf("flag1\n");
    top_struct->count = 0;
    int i = 0;
    for (p = proc; p < &proc[NPROC]; ++p ) {
//        printf("flag2\n");
        if(p->state == UNUSED){
            ++i;
            continue;
        }
//        printf("flag3\n");
        top_struct->count ++;
        strncpy(top_struct->processes[i].name, p->name,16);
        top_struct->processes[i].state = p->state;
        top_struct->processes[i].pid = p->pid;
        if(p->parent != 0){
            top_struct->processes[i].ppid = p->parent->pid;
        } else top_struct->processes[i].ppid = 0;
//        printf("flag4\n");
        top_struct->processes[i].usage = *p->usage_time;
        ++i;
    }
//    printf("flag5\n");

    // sort:
    int n = top_struct->count;
    for (i = 1; i < n; ++i) {
        for (int j = i; j > 0; --j) {

            if(top_struct->processes[j].usage.sum_of_ticks < top_struct->processes[j - 1].usage.sum_of_ticks){
                struct proc_info temp;
                temp = top_struct->processes[j - 1];
                top_struct->processes[j - 1] = top_struct->processes[j];
                top_struct->processes[j] = temp;
            }
        }

    }
    return 0;
}