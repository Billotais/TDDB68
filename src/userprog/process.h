#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

// Steuct used for sync between child and parent for exec
struct parent_child {
    semaphore sema;
    char* file_name;
    bool success;
}

#endif /* userprog/process.h */
