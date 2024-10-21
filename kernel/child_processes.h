struct proc_info {
    char name[16];
    int pid;
    int ppid;
    enum procstate state;
};

struct  child_processes {
    int count;
    struct proc_info processes[NPROC];
};