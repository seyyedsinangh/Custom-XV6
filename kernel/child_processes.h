struct proc_info_cps {
    char name[16];
    int pid;
    int ppid;
    enum procstate state;
    struct cpu_usage usage;
};

struct  child_processes {
    int count;
    struct proc_info_cps processes[NPROC];
};