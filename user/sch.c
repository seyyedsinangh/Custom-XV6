#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/spinlock.h"
#include "kernel/riscv.h"
#include "kernel/proc.h"
//#include "kernel/defs.h"
#include "user.h"


int main() {

//    if (fork_deadline(10) == 0) {
//
//        printf("this is parent 1!\n");
//        for (int i = 0; i < 900000000; ++i){}
//        printf("this is parent 2!\n");
//
//    } else {
//        for (int i = 0; i < 900000000; ++i){}
//        printf("this is kid with deadline\n");
//    }

    if (fork() == 0) {
        set_cpu_quota(3,1);
        sleep(5);
        for (int i = 0; i < 1000000000; ++i){}
        for (int i = 0; i < 900000000; ++i){}
        for (int i = 0; i < 900000000; ++i){}
        int u = cpu_used();
        printf("USAGE:%d\n",u);
        for (int i = 0; i < 10; ++i) {
            printf("this is from quota-set proc!\n");
        }
        sleep(200);
    } else {
        set_cpu_quota(4,100);
        sleep(5);
        for (int i = 0; i < 1000000000; ++i){}
        for (int i = 0; i < 900000000; ++i){}
        for (int i = 0; i < 900000000; ++i){}
        int u = cpu_used();
        printf("USAGE(kid):%d\n",u);
        for (int i = 0; i < 10; ++i) {
            printf("this is from without quota proc!\n");
        }
    }

}
