#ifndef REPORT_TRAPS_H
#define REPORT_TRAPS_H

#define MAX_REPORT_BUFFER_SIZE 10
#define MAX_PARENT_BUFFER_SIZE 4

struct report {
    char pname[16];
    int pid;
    uint64 scause;
    uint64 sepc;
    uint64 stval;
    int parents[MAX_PARENT_BUFFER_SIZE];
    int parents_count;
};

struct report_list {
    struct report reports[MAX_REPORT_BUFFER_SIZE];
    int numberOfReports;
    int writeIndex;
};

extern struct report_list _internal_report_list;

struct report_traps {
    struct report reports[MAX_REPORT_BUFFER_SIZE];
    int count;
};

#endif