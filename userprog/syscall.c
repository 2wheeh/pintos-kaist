#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "lib/user/syscall.h"       // for pid_t
#include "filesys/filesys.h"		// filesys 

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* proto- */
void halt_handler (struct intr_frame *);
void exit_handler (struct intr_frame *);
void fork_handler (struct intr_frame *);
void exec_handler (struct intr_frame *);
void wait_handler (struct intr_frame *);
void create_handler (struct intr_frame *);
void remove_handler (struct intr_frame *);
void open_handler (struct intr_frame *);
void filesize_handler (struct intr_frame *);
void read_handler (struct intr_frame *);
void write_handler (struct intr_frame *);
void seek_handler (struct intr_frame *);
void tell_handler (struct intr_frame *);
void close_handler (struct intr_frame *);

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

/* macros for registers values in intr_frame */
#define SYSCALL_NUM f->R.rax
#define ARG1        f->R.rdi
#define ARG2        f->R.rsi    
#define ARG3        f->R.rdx  
#define ARG4        f->R.r10    
#define ARG5        f->R.r8    
#define ARG6        f->R.r9    
#define RET_VAL	    f->R.rax	// same as SYSCALL_NUM

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
    

    struct action {
        uint64_t syscall_num;
        void (*function) (struct intr_frame *);
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

    actions[SYSCALL_NUM].function(f);
    // printf ("system call!\n");
    // thread_exit ();
}

void
halt_handler (struct intr_frame *f) {
    power_off();
}

void
exit_handler (struct intr_frame *f) {
    int status = (int) ARG1;
	struct thread *curr = thread_current();
	curr->exit_status = status;
    thread_exit();
}

void
fork_handler (struct intr_frame *f){
    const char *thread_name = (char *) ARG1;

}

void
exec_handler (struct intr_frame *f) {
    const char *file = (char *) ARG1;
	
}

void
wait_handler (struct intr_frame *f) {
    pid_t pid = (pid_t) ARG1;
}

void
create_handler (struct intr_frame *f) {
    const char *file;
	unsigned initial_size = (unsigned) ARG2;
	struct thread *curr = thread_current();
	bool success;

	if(!(ARG1 
		&& is_user_vaddr(ARG1) 
		&& pml4_get_page (curr->pml4, ARG1))) { /* file : NULL */
		curr->exit_status = -1;
		thread_exit();
	} 
	else { 
		file = (char *)ARG1;
		success = filesys_create (file, initial_size);
	} 

	RET_VAL = success;
}

void
remove_handler (struct intr_frame *f) {
    const char *file = (char *) ARG1;
	
}

void
open_handler (struct intr_frame *f) {
    const char *file;
	struct thread *curr = thread_current();
	struct file *file_ptr;
	int fd;

	if(!(ARG1 
		&& is_user_vaddr(ARG1) 
		&& pml4_get_page (curr->pml4, ARG1))) { /* file : NULL */
		curr->exit_status = -1;
		thread_exit();
	} 
	else { 
		file = (char *)ARG1;
		file_ptr = filesys_open (file);
		
		if(file_ptr){
			int i = 3;
			
			while (curr->fd_array[i]) { // look up null
				i++;
			}
			
			curr->fd_array[i] = file_ptr;
			fd = i;

			RET_VAL = fd; 
			return;
		}
	}

	RET_VAL = -1; 
}

void
filesize_handler (struct intr_frame *f) {
    int fd = (int) ARG1;
}

void
read_handler (struct intr_frame *f) {
    int fd = (int) ARG1;  
	void *buffer = (void *) ARG2; 
	unsigned size = (unsigned) ARG3;
}

void
write_handler (struct intr_frame *f) {
    // int fd, const void *buffer, unsigned size
    int fd = (int) ARG1;
    const void *buffer = (void *) ARG2;
    unsigned size = (unsigned) ARG3;
	// putbuf();
    printf("%s", buffer);
}

void
seek_handler (struct intr_frame *f) {
    int fd = (int) ARG1; 
	unsigned position = (unsigned) ARG2;
}

void
tell_handler (struct intr_frame *f) {
    int fd = (int) ARG1;
}

void
close_handler (struct intr_frame *f) {
    int fd = (int) ARG1;
	// fd가 open 된 건지 확인
	// fd_array[fd] 를 null 로 바꿔준 

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

