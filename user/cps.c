#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/spinlock.h"
#include "kernel/riscv.h"
#include "kernel/proc.h"
#include "kernel/stat.h"
#include "kernel/child_processes.h"
#include "user/user.h"


int main(int argc, char *argv[]){
    char *states[6] = {"UNUSED", "USED", "SLEEPING", "RUNNABLE", "RUNNING", "ZOMBIE"};
    struct child_processes cps;
    int pid = fork();
    if (pid<0) printf("Fork failed");
    else if (pid==0) {
        fork();
        fork();
        fork();
        fork();
        sleep(100);
    } else {
        sleep(10);
        int error = child_processes(&cps);
        if (error != 0){
            return -1;
        }
        printf("Number of children: %d\n", cps.count);
        printf("PID\tPPID\tSTATE\t\tNAME\n");
        for (int i=0; i<cps.count; i++) {
            struct proc_info pinfo = cps.processes[i];
            if(pinfo.state==2 || pinfo.state==3) printf("%d\t%d\t%s\t%s\n",pinfo.pid,pinfo.ppid,states[pinfo.state],pinfo.name);
            else printf("%d\t%d\t%s\t\t%s\n",pinfo.pid,pinfo.ppid,states[pinfo.state],pinfo.name);
        }
    }
    return 0;
}

