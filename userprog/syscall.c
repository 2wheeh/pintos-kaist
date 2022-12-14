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
#include "vm/vm.h"

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

// 3주차 추가
void mmap_handler (struct intr_frame *);
void munmap_handler (struct intr_frame *);

/* helper functions proto */
void error_exit (void);
bool is_bad_fd  (int);
bool is_bad_ptr (void *, bool);

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

/* macros for fd validity check */
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
    
	#ifdef VM
		//유저가 시스템콜을 요청했을거야.
		//그럼 syscall_handler가 그 요청을 잡았겠지?
		//그리고 현재 스레드에 rsp_stack이라는 변수에다가 유저가 시스템콜 쏘기 직전까지 레지스터가 쓰던 정보 중
		//rsp에 대한 정보를 담는다.
		//그리고 시스템콜 들어가서 커널이 작업하다가 레지스터를 막 오염시키겠지?
		//그러다가 페이지폴트가 뜨면? 유저에 스택을 늘릴지 말지를 판단해야하는데
		//지금 레지스터에 있는 스택포인터 rsp는 커널이 이미 사용하면서 더럽혀져있어서 사용할 수 없고
		//따라서 스레드에 rsp_stack에다가 미리 저장해놓은 rsp를 가지고 이 페이지폴트가 찐인지 가짜인지 판단하고
		//가짜이면 유저풀에서 프레임하나 더 받아서 stack_growth해주면 된다.
		thread_current()->rsp_stack = f->rsp;
	#endif

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
		//3주차 추가
		{SYS_MMAP, mmap_handler},                	/* Map a file into memory*/
		{SYS_MUNMAP, munmap_handler},               /* Remove a memory mapping */
    };
    actions[SYSCALL_NUM].function(f);
}

// void *mmap (void *addr, size_t length, int writable, int fd, off_t offset)
//		 mmap ((char *) 0x10000000, 4096, 0, 1234,0) mmap-read테스트 케이스 시스템콜 요청 샘플
//fd로 열린 파일의 오프셋 파이트로부터 length바이트 만큼을 가상주소 addr에 매핑함.
void mmap_handler (struct intr_frame *f) {
	//ARG1 = addr
	//ARG2 = length
	//ARG3 = writable
	//ARG4 = fd
	//ARG5 = offset

	//필터링해야하는 케이스
	//열린파일의 길이가 0이다 (ARG2가 0인 경우, (long long)ARG2<= 0)
	//페이지 단위가 아닌 곳에 매핑하려하는 경우 (addr이 page_aligned되지 않은 경우, pg_round_down(ARG1) != ARG1)
	//이미 매핑되어있는데 또 매핑해달라고 유저가 요청한 경우 (spt_find_page(&thread_current()->spt, ARG1))
	//addr이 0인경우 (리눅스는 addr이 0이면 mmap을 시도하려한대, ARG1 == NULL)
	//addr이 커널영역인 경우 (유저가 커널영역에 매핑하려는 경우를 차단해야함, is_kernel_vaddr(ARG1), tests/vm/mmap-kernel테스트 케이스)
	//파일이 열리지 않은 경우 (fd가 NULL인경우, target == NULL)
	struct thread *curr = thread_current();
	struct file *target = fd_file(ARG4);

	if(ARG4 == 0 || ARG4==1){ //콘솔입출력을 의미하는 fd는 매핑할 수 없음.
		error_exit();
	}

	if(ARG5 % PGSIZE != 0 				//파일의 커서가 offset인데 페이지 단위가 아니면 리턴
		||pg_round_down(ARG1) != ARG1 	//addr이 페이지 	
		||is_kernel_vaddr(ARG1)
		||ARG1 == NULL
		||(long long)ARG2<= 0
		||spt_find_page(&thread_current()->spt, ARG1)
		||target == NULL
	){
		RET_VAL = NULL;
		return;
	}
	void *ret = do_mmap(ARG1, ARG2, ARG3, target, ARG5);
	RET_VAL = ret;
	return;
}

void
munmap_handler (struct intr_frame *f) {
	do_munmap(ARG1);
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
	RET_VAL = process_fork (thread_name, f);
}

void
exec_handler (struct intr_frame *f) {
    const char *file = (char *) ARG1;
	const char *fn_copy;
	bool success;

	if(is_bad_ptr(file, false)) {
		RET_VAL = -1;
		error_exit();
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
	bool success;

	if (is_bad_ptr(file, false)) { /* file : NULL */
		RET_VAL = false;
		error_exit();
	} 
	else { 
		lock_acquire(&filesys_lock);
		success = filesys_create (file, initial_size);
		lock_release(&filesys_lock);
		RET_VAL = success;
		return;
	} 

	RET_VAL = false;
}

void
remove_handler (struct intr_frame *f) {
    const char *file = (char *) ARG1;

	if(is_bad_ptr(file, false)){
		RET_VAL = false;
		error_exit();
	} else {
		lock_acquire(&filesys_lock);
		RET_VAL = filesys_remove(file);
		lock_release(&filesys_lock);
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

	if(is_bad_ptr(file, false)) { /* file : NULL */
		RET_VAL = -1;
		error_exit();
	} 
	else { 
		lock_acquire(&filesys_lock);
		file_ptr = filesys_open (file);
		lock_release(&filesys_lock);

		if (file_ptr){
			int i = FD_MIN;
			
			lock_acquire(&filesys_lock);
			while (fd_file(i)) { // look up null
				i++;
				if (i==FD_MAX) {
					file_close (file_ptr);
					RET_VAL = -1;
					return;
					}
			}
			lock_release(&filesys_lock);
			
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

	lock_acquire(&filesys_lock);
	RET_VAL = file_length(file_ptr);
	lock_release(&filesys_lock);
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
		|| is_bad_ptr(buffer, true)
		|| is_bad_ptr(buffer+size-1, true)) 			// buffer valid check
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

	if (is_STDOUT(fd)) { /* 표준 입력 : 커널이 콘솔에 쓰려할 때 */
		putbuf(buffer, size);
	} 
	else if (is_bad_fd(fd)
		|| is_STDIN(fd)
		|| !(file_ptr = fd_file(fd))
		|| is_bad_ptr(buffer, false)
		|| is_bad_ptr(buffer+size-1, false))
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
	}

	file = fd_file(fd);

	lock_acquire(&filesys_lock);
	file_seek(file, position);
	lock_release(&filesys_lock);
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

		lock_acquire(&filesys_lock);
		file_close(file_ptr);
		fd_file(fd) = NULL;
		lock_release(&filesys_lock);
	}
}

void error_exit() {
	struct thread *curr = thread_current();
	curr->exit_status = -1;
	thread_exit();
}

bool 
is_bad_fd (int fd) {
	bool is_valid;
	is_valid = (fd && (FD_MIN<= fd) && (fd < FD_MAX));

	return !is_valid;
}

bool 
is_bad_ptr (void *ptr, bool to_write) {
	bool is_valid;
	struct thread *curr = thread_current();
	struct page *e_page;

	if (to_write) {
		is_valid = (ptr && is_user_vaddr(ptr) && (e_page = spt_find_page (&curr->spt, ptr)) && e_page->writable);
	}
	else {
		is_valid = (ptr && is_user_vaddr(ptr) && (e_page = spt_find_page (&curr->spt, ptr)));
	}

	return !is_valid;
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