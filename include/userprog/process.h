#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

struct container {
    struct file *file;  
    off_t offset;           // 해당 파일의 오프셋
    size_t page_read_bytes; // 읽어올 파일의 데이터 크기(load_segment에서 1PAGE보다는 작거나 같다)
};

#endif /* userprog/process.h */
