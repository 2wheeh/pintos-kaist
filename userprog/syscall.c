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
#include "filesys/file.h"
#include "include/lib/user/syscall.h"
#include "lib/string.h"				// strlcpy 필수
#include "userprog/process.h"
#include "threads/palloc.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void custom_dump_frame(struct intr_frame *f);

/* syscall handler functions proto */
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

/* helper functions proto */
void error_exit(void);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */
/*
시스템 호출.
  이전에는 시스템 호출 서비스가 인터럽트 핸들러에 의해 처리되었습니다. (예: Linux의 int 0x80). 
  그러나 x86-64에서 제조업체는 시스템 호출을 요청하기 위한 효율적인 경로인 `syscall` 명령을 제공합니다.
  syscall 명령어는 모델별 레지스터(MSR)에서 값을 읽어서 작동합니다. 자세한 내용은 설명서를 참조하십시오.
*/

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

/* macros for ptr (ARG1) validity check */
#define is_valid_ptr(ptr)   (ptr && is_user_vaddr(ptr) && pml4_get_page (curr->pml4, ptr))
#define is_bad_ptr(ptr)	    (!is_valid_ptr(ptr))

/* macros for fd validity check */
#define is_valid_fd(fd)		(fd && (FD_MIN<= fd) && (fd < FD_MAX))
#define is_bad_fd(fd)		(!is_valid_fd(fd))
#define is_STDIN(FD)		(fd == STDIN_FILENO)
#define is_STDOUT(fd)		(fd == STDOUT_FILENO)

/* macro for reference fd_array */
#define fd_file(fd)			(curr->fd_array[fd])

void
syscall_init (void) {
    write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
            ((uint64_t)SEL_KCSEG) << 32);
    write_msr(MSR_LSTAR, (uint64_t) syscall_entry); //


	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	/* 인터럽트 서비스 루틴은 syscall_entry가 사용자 영역 스택을 커널 모드 스택으로 교체할 때까지 인터럽트를 제공하지 않아야 합니다. 
	따라서 FLAG_FL을 마스킹했습니다.*/
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	
	lock_init(&filesys_lock);
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
	if(curr->tid == 420) printf("%d, 받은거 = %d\n", curr->exit_status, status);
	// curr->my_parent->child_will = status;
	// curr->my_parent->my_child = NULL;
    thread_exit();
}

void
fork_handler (struct intr_frame *f){
    const char *thread_name = (char *) ARG1;
	RET_VAL = process_fork (thread_name, f);
}

void
exec_handler (struct intr_frame *f) {
    const char *file = (char *) ARG1;
	const char *fn_copy;
	struct thread *curr = thread_current();
	bool success;

	if(is_bad_ptr(file)) {
		RET_VAL = -1;
		return;
	}

	fn_copy = palloc_get_page (0);
	strlcpy (fn_copy, file, PGSIZE);

	success = process_exec (fn_copy);
		
	if(success) {
		RET_VAL = success;
	}
	else {
		RET_VAL = -1;
	}
}

void
wait_handler (struct intr_frame *f) {
    pid_t pid = (pid_t) ARG1;
	RET_VAL = process_wait (pid);
}

void
create_handler (struct intr_frame *f) {
    const char *file = (char *) ARG1;
	unsigned initial_size = (unsigned) ARG2;
	struct thread *curr = thread_current();
	bool success;

	if (is_bad_ptr(file)) { /* file : NULL */
		RET_VAL = false;
		error_exit();
	} 
	else { 
		success = filesys_create (file, initial_size);
		RET_VAL = success;
		return;
	} 

	RET_VAL = false;
}

void
remove_handler (struct intr_frame *f) {
    const char *file = (char *) ARG1;
	struct thread *curr = thread_current();

	if(is_bad_ptr(file)){
		RET_VAL = false;
		error_exit();
	} else {
		RET_VAL = filesys_remove(file);
		return;
	}

	RET_VAL = false;
}

void
open_handler (struct intr_frame *f) {
    const char *file = (char *) ARG1;
	struct thread *curr = thread_current();
	struct file *file_ptr;
	int fd;

	if(is_bad_ptr(file)) { /* file : NULL */
		error_exit();
	} 
	else { 
		file_ptr = filesys_open (file);
		
		if (file_ptr){
			int i = FD_MIN;
			
			while (fd_file(i)) { // look up null
				i++;
				if (i==FD_MAX) {
					file_close (file_ptr);
					RET_VAL = -1;
					return;
					}
			}
			
			fd_file(i) = file_ptr;
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
	struct thread *curr = thread_current();
	struct file *file_ptr = fd_file(fd);

	ASSERT(fd != NULL);
	ASSERT(file_ptr != NULL);

	RET_VAL = file_length(file_ptr);
}

void
read_handler (struct intr_frame *f) {
    int fd = (int) ARG1;  
	void *buffer = (void *) ARG2; 
	unsigned size = (unsigned) ARG3;
	struct file *file_ptr;
	struct thread *curr = thread_current();

	if (is_bad_fd(fd) 
		|| is_STDOUT(fd) 
		|| !(file_ptr = fd_file(fd))
		|| is_bad_ptr(buffer)) 			// buffer valid check
	{
		RET_VAL = -1;
		error_exit();
	} else {
		lock_acquire(&filesys_lock);
		RET_VAL = file_read(file_ptr, buffer, size);
		lock_release(&filesys_lock);
	}
}

void
write_handler (struct intr_frame *f) {
    int fd = (int) ARG1;
    const void *buffer = (void *) ARG2;
    unsigned size = (unsigned) ARG3;
	struct file *file_ptr;
	struct thread *curr = thread_current();
	// putbuf();
    // printf("%s", buffer);
	if (is_STDOUT(fd)) { /* 표준 입력 : 커널이 콘솔에 쓰려할 때 */
		putbuf(buffer, size);
	} 
	else if (is_bad_fd(fd)
		|| is_STDIN(fd)
		|| !(file_ptr = fd_file(fd))
		|| is_bad_ptr(buffer))
	{
		RET_VAL = 0;
		error_exit();
	} else {
		lock_acquire(&filesys_lock);
		RET_VAL = file_write (file_ptr, buffer, size);
		lock_release(&filesys_lock);
	}
}

void
seek_handler (struct intr_frame *f) {
    int fd = (int) ARG1; 
	unsigned position = (unsigned) ARG2;
	struct file *file;
	struct thread *curr = thread_current();

	if(is_bad_fd(fd)) {
		error_exit();
		return;
	}

	file = fd_file(fd);

	file_seek(file, position);
}

void
tell_handler (struct intr_frame *f) {
    int fd = (int) ARG1;
}

void
close_handler (struct intr_frame *f) {
    int fd = (int) ARG1;
	struct thread *curr = thread_current();
	struct file *file_ptr;
	// fd가 open 된 건지 확인 (fd_array[fd] 가 null이 아님)
	// 그렇다면 file_close 후
	// fd_array[fd] 를 null 로 바꿔줌 

	if (is_bad_fd(fd) || !(file_ptr = fd_file(fd))) { /* fd valid check */
		error_exit();
	} 
	else {
		ASSERT(file_ptr != NULL);

		file_close(file_ptr);
		fd_file(fd) = NULL;
	}


}

void error_exit() {
	struct thread *curr = thread_current();
	curr->exit_status = -1;
	thread_exit();
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

