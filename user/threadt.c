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
}

int main(int argc, char *argv[]) {
    int ta = create_thread(my_thread, (void *) &a);
    int tb = create_thread(my_thread, (void *) &b);
    int tc = create_thread(my_thread, (void *) &c);
    join_thread(ta);
    join_thread(tb);
    join_thread(tc);
    exit(0);
}

