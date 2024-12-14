#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/spinlock.h"
#include "kernel/riscv.h"
#include "kernel/proc.h"
#include "kernel/stat.h"
#include "kernel/child_processes.h"
#include "user/user.h"

volatile int a = 0, b = 0, c = 0;

void *my_thread(void *arg) {
//    int *number = arg;
//    for (int i = 0; i < 10; ++i) {
//        (*number)++;
//        if (number == &a) {
//            printf("thread a: %d\n", *number);
//        } else if (number == &b) {
//            printf("thread b: %d\n", *number);
//        } else {
//            printf("thread c: %d\n", *number);
//        }
//    }
//    if (arg == &a) {
//        printf("thread a: %p(%d)\n", arg, *((int *)arg));
//    } else if (arg == &b) {
//        printf("thread b: %p(%d)\n", arg, *((int *)arg));
//    } else {
//        printf("thread c: %p(%d)\n", arg, *((int *)arg));
//    }

//    while (1);
    int number = 0;
    for (int i = 0; i < 100; ++i) {
        number++;
        if (arg == &a) {
            printf("thread a: %d\n", number);
        } else if (arg == &b) {
            printf("thread b: %d\n", number);
        } else if (arg == &c) {
            printf("thread c: %d\n", number);
        } else {
            printf("thread arg not correct: %d\n", number);
        }
    }
    return (void *) arg;
}

//void *my_thread2(void *arg) {
//    if (arg == &b) {
//        printf("this is thread b: %p(%d)\n", arg, *((int *)arg));
//    }
//    while (1);
//    return (void *) arg;
//}


int main(int argc, char *argv[]) {
    int astack[100];
    int bstack[100];
    int cstack[100];
//    void * astack = malloc(THREAD_STACK_SIZE);
//    void * bstack = malloc(THREAD_STACK_SIZE);
//    void * cstack = malloc(THREAD_STACK_SIZE);
//    int ta, tb, tc;
    int ta = create_thread(my_thread, (void *) &a,(void *) &astack[100]);
    int tb = create_thread(my_thread, (void *) &b, (void *) &bstack[100]);
    int tc = create_thread(my_thread, (void *) &c, (void *) &cstack[100]);
    join_thread(ta);
    join_thread(tb);
    join_thread(tc);
    printf("main thread before end!\n");
    exit(0);
}

