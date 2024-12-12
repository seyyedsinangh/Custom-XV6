#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "child_processes.h"
#include "report_traps.h"

uint64
sys_exit(void)
{
    int n;
    argint(0, &n);
    exit(n);
    return 0;  // not reached
}

uint64
sys_getpid(void)
{
    return myproc()->pid;
}

uint64
sys_fork(void)
{
    return fork();
}

uint64
sys_wait(void)
{
    uint64 p;
    argaddr(0, &p);
    return wait(p);
}

uint64
sys_sbrk(void)
{
    uint64 addr;
    int n;

    argint(0, &n);
    addr = myproc()->sz;
    if(growproc(n) < 0)
        return -1;
    return addr;
}

uint64
sys_sleep(void)
{
    int n;
    uint ticks0;

    argint(0, &n);
    if(n < 0)
        n = 0;
    acquire(&tickslock);
    ticks0 = ticks;
    while(ticks - ticks0 < n){
        if(killed(myproc())){
            release(&tickslock);
            return -1;
        }
        sleep(&ticks, &tickslock);
    }
    release(&tickslock);
    return 0;
}

uint64
sys_kill(void)
{
    int pid;

    argint(0, &pid);
    return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
    uint xticks;

    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);
    return xticks;
}

uint64
sys_child_processes(void)
{
    struct child_processes *cps;
    struct child_processes kcps;
    argaddr(0, (uint64 *)&cps);
    struct proc *p = myproc();
    copyin(p->pagetable, (char *)cps, (uint64)&kcps, sizeof(kcps));
    acquire(&tickslock);
    int err = child_processes(&kcps);
    release(&tickslock);
    copyout(p->pagetable, (uint64)cps, (char *)&kcps, sizeof(kcps));
    return err;
}

uint64
sys_report_traps(void)
{
    struct report_traps *reptraps;
    struct report_traps kreptraps;
    argaddr(0, (uint64 *)&reptraps);
    struct proc *p = myproc();
    copyin(p->pagetable, (char *)reptraps, (uint64)&kreptraps, sizeof(kreptraps));
    acquire(&tickslock);
    int err = report_traps(&kreptraps);
    release(&tickslock);
    copyout(p->pagetable, (uint64)reptraps, (char *)&kreptraps, sizeof(kreptraps));
    return err;
}

uint64
sys_create_thread(void)
{
    uint64 start_routine_addr;
    uint64 arg_addr;
    uint64 pstack;
    argaddr(0, &start_routine_addr);
    argaddr(1, &arg_addr);
    argaddr(2, &pstack);
    int tid = create_thread((void (*)(void *))start_routine_addr, (void *)arg_addr, (void *)pstack);
    return tid;
}

uint64
sys_join_thread(void)
{
    int tid;
    argint(0, &tid);
    int res = join_thread(tid);
    return res;
}

uint64
sys_exit_thread(void)
{
    int tid;
    argint(0, &tid);
    int res = exit_thread(tid);
    return res;
}

