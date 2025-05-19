#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"

struct wait_status {
    struct lock lock;               // 상태 보호용 락
    struct semaphore dead;          // 자식이 종료될 때 down됨
    int exit_code;                  // 자식의 종료 상태
    int ref_cnt;                    // 참조 카운트 (부모와 자식 둘 다 해제하면 free)
    struct list_elem elem;          // 부모의 자식 리스트용
    tid_t tid;                      // 자식의 tid
    bool exited;                    // 자식이 종료되었는지 여부
};

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

#endif /* userprog/process.h */
