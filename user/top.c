#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/spinlock.h"
#include "kernel/riscv.h"
#include "kernel/proc.h"
//#include "kernel/defs.h"
#include "user.h"

static char *states[] = {
        [UNUSED]    "unused",
        [USED]      "used",
        [SLEEPING]  "sleep ",
        [RUNNABLE]  "runble",
        [RUNNING]   "run   ",
        [ZOMBIE]    "zombie"
};

int main() {
    struct top top_struct;

    int id = fork();
    if (id == 0) {

        for (int i = 0; i < 1000000000; ++i) {

        }
        top_func(&top_struct);

    } else {
        for (int i = 0; i < 900000000; ++i) {

        }
        for (int i = 0; i < 900000000; ++i) {

        }

        sleep(20);
        exit(0);
    }
    int n = top_struct.count;
    printf("number of process:%d\nPID\t\tPPID\t\tSTATE\t\tNAME\t\tUSAGE\n",n);
    for (int j = n - 1; j >= 0 ; --j) {
        printf("%d\t\t%d\t\t%s\t\t%s\t\t%d\n", top_struct.processes[j].pid,top_struct.processes[j].ppid,states[top_struct.processes[j].state],top_struct.processes[j].name,top_struct.processes[j].usage.sum_of_ticks);
    }
    sleep(20);
}
