#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/spinlock.h"
#include "kernel/riscv.h"
#include "kernel/proc.h"
#include "kernel/stat.h"
#include "kernel/report_traps.h"
#include "user/user.h"


int main(int argc, char *argv[]){
    struct report_traps reptraps;
    int pid = fork();
    if (pid<0) printf("Fork failed");
    else if (pid==0) {
        if (fork()==0) {
            int *x = 0;
            *x = 2;
        } else {
            if (fork()==0) {
                if (fork()==0) {
                    int *x = (int *) 11111;
                    printf("%d\n",*x);
                }
            }
            if (fork()==0) {
                if (fork()==0) {
                    if (fork()==0) {
                        int *x = (int *) 34234;
                        printf("%d\n",*x);
                    }
                }
            }
        }
        sleep(100);
    } else {
        sleep(10);
        int error = report_traps(&reptraps);
        if (error != 0){
            return -1;
        }
        printf("Number of exceptions: %d\n", reptraps.count);
        printf("PID\tPPID\tPNAME\t\tSCAUSE\t\tSEPC\t\tSTVAL\n");
        for (int i=0; i<reptraps.count; i++) {
            struct report rp  = reptraps.reports[i];
            printf("%d\t%d\t%s\t0x%lx\t\t0x%lx\t\t0x%lx\n",rp.pid,rp.parents[0],rp.pname,rp.scause,rp.sepc,rp.stval);
        }
    }
    return 0;
}