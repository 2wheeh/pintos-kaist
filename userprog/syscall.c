#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
// #include "lib/user/syscall.h"        // for pid_t

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

struct arguments {
    uint64_t syscall_num;
    uint64_t arg1, arg2, arg3, arg4, arg5 ,arg6;
};

/* proto- */
void halt_handler (struct arguments *sysarg);
void exit_handler (struct arguments *sysarg);
void fork_handler (struct arguments *sysarg);
void exec_handler (struct arguments *sysarg);
void wait_handler (struct arguments *sysarg);
void create_handler (struct arguments *sysarg);
void remove_handler (struct arguments *sysarg);
void open_handler (struct arguments *sysarg);
void filesize_handler (struct arguments *sysarg);
void read_handler (struct arguments *sysarg);
void write_handler (struct arguments *sysarg);
void seek_handler (struct arguments *sysarg);
void tell_handler (struct arguments *sysarg);
void close_handler (struct arguments *sysarg);


/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */


void
syscall_init (void) {
    write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
            ((uint64_t)SEL_KCSEG) << 32);
    write_msr(MSR_LSTAR, (uint64_t) syscall_entry); //

    /* The interrupt service rountine should not serve any interrupts
     * until the syscall_entry swaps the userland stack to the kernel
     * mode stack. Therefore, we masked the FLAG_FL. */
    write_msr(MSR_SYSCALL_MASK,
            FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
    // TODO: Your implementation goes here.
    // run_actions() in threads/init.c 참고
    
    struct arguments sysarg;

    struct action {
        uint64_t syscall_num;
        void (*function) (struct arguments *sysarg);
    };

    static const struct action actions[] = {
        {SYS_HALT, halt_handler},                   /* Halt the operating system. */
        {SYS_EXIT, exit_handler},                   /* Terminate this process. */
        {SYS_FORK, fork_handler},                   /* Clone current process. */
        {SYS_EXEC, exec_handler},                   /* Switch current process. */
        {SYS_WAIT, wait_handler},                   /* Wait for a child process to die. */
        {SYS_CREATE, create_handler},               /* Create a file. */
        {SYS_REMOVE, remove_handler },              /* Delete a file. */
        {SYS_OPEN, open_handler},                   /* Open a file. */
        {SYS_FILESIZE, filesize_handler},           /* Obtain a file's size. */
        {SYS_READ, read_handler},                   /* Read from a file. */
        {SYS_WRITE, write_handler},                 /* Write to a file. */
        {SYS_SEEK, seek_handler},                   /* Change position in a file. */
        {SYS_TELL, tell_handler},                   /* Report current position in a file. */
        {SYS_CLOSE, close_handler},                 /* Close a file. */
    };

    sysarg.syscall_num = f->R.rax; 
    sysarg.arg1 = f->R.rdi;
    sysarg.arg2 = f->R.rsi;
    sysarg.arg3 = f->R.rdx;
    sysarg.arg4 = f->R.r10;
    sysarg.arg5 = f->R.r8;
    sysarg.arg6 = f->R.r9;

    actions[sysarg.syscall_num].function(&sysarg);
    // printf ("system call!\n");
    // thread_exit ();
}

void
halt_handler (struct arguments *sysarg) {
    power_off();
}

void
exit_handler (struct arguments *sysarg) {
    // int status   
	printf("%s: exit(0)\n", thread_current()->name);
    thread_exit();
}

void
fork_handler (struct arguments *sysarg){
    // const char *thread_name
}

void
exec_handler (struct arguments *sysarg) {
    // const char *file
}

void
wait_handler (struct arguments *sysarg) {
    // pid_t pid
}

void
create_handler (struct arguments *sysarg) {
    // const char *file, unsigned initial_size
}

void
remove_handler (struct arguments *sysarg) {
    // const char *file
}

void
open_handler (struct arguments *sysarg) {
    // const char *file
}

void
filesize_handler (struct arguments *sysarg) {
    // int fd
}

void
read_handler (struct arguments *sysarg) {
    // int fd, void *buffer, unsigned size
}

void
write_handler (struct arguments *sysarg) {
    // int fd, const void *buffer, unsigned size
    int fd = (int) sysarg->arg1;
    const void *buffer = (void *) sysarg->arg2;
    unsigned size = (unsigned) sysarg->arg3;

    printf("%s", buffer);
}

void
seek_handler (struct arguments *sysarg) {
    // int fd, unsigned position
}

void
tell_handler (struct arguments *sysarg) {
    // int fd
}

void
close_handler (struct arguments *sysarg) {
    // int fd
}


// 여기까지가 pjt 2 구현 범위

/*****************************************************
void
dup2_handler (int oldfd, int newfd){
    // return syscall2 (SYS_DUP2, oldfd, newfd);
}

void *
mmap_handler (void *addr, size_t length, int writable, int fd, off_t offset) {
    // return (void *) syscall5 (SYS_MMAP, addr, length, writable, fd, offset);
}

void
munmap_handler (void *addr) {
    // syscall1 (SYS_MUNMAP, addr);
}

void
chdir_handler (const char *dir) {
    // return syscall1 (SYS_CHDIR, dir);
}

void
mkdir_handler (const char *dir) {
    // return syscall1 (SYS_MKDIR, dir);
}

void
readdir_handler (int fd, char name[READDIR_MAX_LEN + 1]) {
    // return syscall2 (SYS_READDIR, fd, name);
}

void
isdir_handler (int fd) {
    // return syscall1 (SYS_ISDIR, fd);
}

void
inumber_handler (int fd) {
    // return syscall1 (SYS_INUMBER, fd);
}

void
symlink_handler (const char* target, const char* linkpath) {
    // return syscall2 (SYS_SYMLINK, target, linkpath);
}

void
mount_handler (const char *path, int chan_no, int dev_no) {
    // return syscall3 (SYS_MOUNT, path, chan_no, dev_no);
}

void
umount_handler (const char *path) {
    // return syscall1 (SYS_UMOUNT, path);
}
*****************************************************************/

