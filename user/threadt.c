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
    int *number = arg;
    for (int i = 0; i < 100; ++i) {
        (*number)++;
        if (number == &a) {
            printf("thread a: %d\n", *number);
        } else if (number == &b) {
            printf("thread b: %d\n", *number);
        } else {
            printf("thread c: %d\n", *number);
        }
    }
    while (1);
    return (void *) number;
}

int main(int argc, char *argv[]) {
    void * astack = malloc(THREAD_STACK_SIZE);
    void * bstack = malloc(THREAD_STACK_SIZE);
    void * cstack = malloc(THREAD_STACK_SIZE);
    int ta, tb, tc;
    create_thread(my_thread, (void *) &a, astack);
    create_thread(my_thread, (void *) &b, bstack);
    create_thread(my_thread, (void *) &c, cstack);
    join_thread(ta);
    join_thread(tb);
    join_thread(tc);
    printf("dddd, %p\n", astack);
    while (1);
    exit(0);
}

