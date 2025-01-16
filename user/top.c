#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/spinlock.h"
#include "kernel/riscv.h"
#include "kernel/proc.h"
//#include "kernel/defs.h"
#include "user.h"

//static char *states[] = {
//        [UNUSED]    "unused",
//        [USED]      "used",
//        [SLEEPING]  "sleep ",
//        [RUNNABLE]  "runble",
//        [RUNNING]   "run   ",
//        [ZOMBIE]    "zombie"
//};

int main() {
    struct top *top_struct = (struct top  *) malloc(sizeof (struct top *));

    int id = fork();
    if (id == 0) {
        for (int i = 0; i < 10000; ++i) {

        }
    } else {
        for (int i = 0; i < 10; ++i) {

        }
        top_proc(top_struct);
        sleep(10);
    }

//    printf("from user!\n");
//    printf("number of process:%d\nPID\t\tPPID\t\tSTATE\t\tNAME\t\tUSAGE\\t\n",top_struct->count);
//    for (int j = 0; j < top_struct->count; ++j) {
//        printf("%d\t\t%d\t\t%s\t\t%s\t\t%d\n", top_struct->processes[j].pid,top_struct->processes[j].ppid,top_struct->processes[j].pid,states[top_struct->processes[j].state],top_struct->processes[j].name,top_struct->processes[j].usage.sum_of_ticks);
//    }


}
