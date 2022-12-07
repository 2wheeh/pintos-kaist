#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "threads/synch.h"
#include "userprog/syscall.h"

#include "lib/user/syscall.h"

#include "lib/stdio.h"	// hex_dump()

#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

static struct passing_args {
	struct intr_frame *parent_f;
	struct thread *be_parent;
	struct semaphore *birth_sema;
};

/* General process initializer for initd and other process. */
/* 일반적인 프로세서 생성자 */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
/* FILE_NAME에서 로드된 "initd"라는 첫 번째 사용자 영역 프로그램을 시작합니다.
process_create_initd()가 반환되기 전에 새 스레드가 스제쥴 될 수 있으며 심지어 종료될 수도 있습니다. 
initd의 스레드 ID를 반환하거나 스레드를 생성할 수 없는 경우 TID_ERROR를 반환합니다. 
이것은 한 번만 호출되어야 합니다.!!!!*/
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy, *sp;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);
	strtok_r (file_name, " ", &sp);	// tread_name에 전달해줄 file_name에서 arg 잘라냈음

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
/* 첫 유저 프로세스를 실행시키는 thread routine func */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
/* 현재 프로세스 이름 그대로 복사, 생성하지 못한다면 TID_ERROR 발생*/
tid_t
process_fork (const char *name, struct intr_frame *if_) {
	/* Clone current thread to new thread.*/
	struct semaphore *birth_sema = (struct semaphore *)malloc(sizeof(struct semaphore));		// 출산 대기를 위한 sema (fork가 모두 끝날떄까지 엄마가 얌전히 기다릴 수 있도록)
	struct passing_args *pa = (struct passing_arg *)malloc(sizeof(struct passing_args));		// 자식이 태어나고 엄마정보를 복제하는 __do_fork에서 사용할 인자들을 담은 구조체 (부모의 user level일때의 기억)
	struct thread *curr = thread_current();

	pa->be_parent = curr; 					// 이것만 malloc 안한 이유 : malloc 해서 복사하면 엄마복사본을 자식이 엄마로 알게됨 그래서 malloc -> memcpy 를 하지 않고 그냥 엄마를 참고하게 함
	
	pa->parent_f= (struct intr_frame *)malloc(sizeof (struct intr_frame));	// 부모가 process_fork 호출하는 시점의 intr_frame (context) 를 자식이 복사할 수 있게 공간(heap)을 할당받음
	memcpy (pa->parent_f, if_, sizeof(struct intr_frame));					// 복사

	pa->birth_sema = birth_sema;											// 부모가 자식의 출산이 끝날때 까지 기다리게 하기위한 sema;
	sema_init(pa->birth_sema, 0);											// sema_down시 바로 waitors에서 기다리게 하기위해 0으로 sema_init

	tid_t result = thread_create (name, PRI_DEFAULT, __do_fork, pa);		// 부모를 복사할 자식 쓰레드 create (생성된 후 __do_fork(pa)를 할 것임)
	
	if(result < 0) {
		free(birth_sema);
		return result;
	}

	// free(pa); //error 재현 코드
	sema_down(birth_sema);												// 자식이 복사과정을 끝내서 부모가 일어남.
	free(birth_sema);
	// free(pa->parent_f);
	// free(pa);

	struct list_elem *elem_just_forked = list_begin(&curr->child_list);
	struct child_info *just_forked; 
	int exit_status_child;
	// 복사과정이 잘 끝났는지 검사하기 위한 과정 
	while (elem_just_forked != list_end(&curr->child_list)) { 				// do_fork 에서 문제없이 잘 끝난건지 검사
		just_forked = list_entry(elem_just_forked, struct child_info, elem_c);
		if (just_forked->tid == result) {
			exit_status_child = just_forked->exit_status;
			break;				
		} 
		elem_just_forked = list_next(elem_just_forked);
	}
	
	if (exit_status_child == EXIT_MY_ERROR) {	// 자식이 do_fork 중 실패해서 thread_exit으로 갔음
		return -1;
	}
	else return result;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
/* 부모의 주소공간을 복제한다. 프로젝트2 에서만 사용*/
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (is_kernel_vaddr(va)) return true;

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page (PAL_USER | PAL_ZERO);

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy (newpage, parent_page, PGSIZE);
	if (is_writable(pte)) writable = true;

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
/* 
부모의 실행 컨텍스트를 복사하는 스레드 함수
힌트) parent->tf는 프로세스의 사용자 영역 컨텍스트를 보유하지 않는다.
이것은. process_fork의 두번재 인자를 이 함수에 전달해야한다는 뜻.
// 확인 필요 : R.rdi ->function , R.rsi-> aux
*/
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) (((struct passing_args *)aux)->be_parent);
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if = ((struct passing_args *)aux)->parent_f;
	struct semaphore *birth_sema = ((struct passing_args *)aux)->birth_sema;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	
	if_.R.rax = 0;

	/* 2. Duplicate PT */
	current->pml4 = pml4_create(); 
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);											// process 를 실행가능하도록 바꿔줌
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	for (int i = FD_MIN; i < FD_MAX; i++) {										// 부모의 fd_array 를 그대로 가져오기
		if (parent->fd_array[i]) {
			current->fd_array[i] = file_duplicate (parent->fd_array[i]);
		} else {
			current->fd_array[i] = NULL;
		}
	}
	current->current_file = file_duplicate(parent->current_file);				// 부모가 실행 중인 file : exec로 실행된 fd가 없는 현재 실행중인 *file

	process_init ();
	// memcpy(&current->tf, &if_, sizeof (struct intr_frame));						// 이 정보가 그대로 쓰일 일은 없긴 함. 단지 부모랑 완전 똑같이 만들어주기위함
	
	/* Finally, switch to the newly created process. */
	if (succ) {
		sema_up(birth_sema);
		free(parent_if);
		free(aux);
		do_iret (&if_);
	}

error:
	sema_up(birth_sema);
	free(parent_if);
	free(aux);
	current->exit_status = -1;
	thread_exit ();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
/* 현재 실행 컨텍스트를 f_name(다른 실행 컨텍스트)으로 전환합니다.*/
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	/* 스레드 구조에서 intr_frame을 사용할 수 없습니다.
	왜냐하면 스레드를 rescheduled할 때 이것은 스레드에게 줄 실행 정보를 저장합니다.*/
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG; // User data Selector
	_if.cs = SEL_UCSEG; // User code selector
	_if.eflags = FLAG_IF | FLAG_MBS; // Flags

	/* We first kill the current context */
	process_cleanup ();

	/* And then load the binary */
	// file_close(thread_current()->current_file);

	success = load (file_name, &_if);
	

	/* If load failed, quit. */
	palloc_free_page (file_name);
	if (!success)
		return -1;
	
	
	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
/*
스레드 TID가 죽을 때까지 기다렸다가 종료 상태를 반환합니다. 
만약에 커널에 의해 종료되면(예외로 인해 종료됨) -1을 반환합니다. 
TID가 유효하지 않거나 호출 프로세스의 자식이 아니거나 주어진 TID에 대해 process_wait()가 이미 성공적으로 호출된 경우 기다리지 않고 즉시 -1을 반환합니다. 
이 기능은 문제 2-2에서 구현될 것이다. 현재로서는 아무 작업도 수행하지 않습니다.
*/
int
process_wait (tid_t child_tid) {
	struct thread *curr = thread_current();
	int ret;

	struct list_elem *elem_zombie = list_begin(&curr->child_list);
	struct child_info *zombie;										   // 죽은 자식의 흔적 (부모가 마저 치워줘야 함)
																	   
	while(elem_zombie != list_end(&curr->child_list)) {	               // child_tid 와 같은 tid의 child_info 를 찾음
		zombie = list_entry(elem_zombie, struct child_info, elem_c);  

		if(zombie->tid == child_tid) {
			
			while (zombie->is_zombie == false)
			{	
				sema_down (&zombie->sema);								// 자식이 죽을 때 까지 부모가 대기
			}															// 이 밑으로는 자식이 무조건 진짜 죽은 후임
			ret = zombie->exit_status;									// 자식이 죽을 때 적어둔 exit_status
			list_remove(elem_zombie);									// 자식이 사용하던 child_info 구조체를 연결리스트에서 빼버림
			free(zombie);												// 자식이 사용하던 child_info 구조체 free 
			return ret;													
		}
		elem_zombie = list_next(elem_zombie);		
	}

	return -1;

}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */


	if(curr->pml4 != NULL) {
		printf("%s: exit(%d)\n", curr->name, curr->exit_status);
	}

	process_cleanup ();		// 본인이 사용한 자원 청소
	// 파일 다 닫기
	lock_acquire(&filesys_lock);
	for (int i = FD_MIN; i < FD_MAX; i++) {
		file_close(curr->fd_array[i]);
	}
	// file_close(curr->current_file);
	lock_release(&filesys_lock);

	// 좀비 청소 + 고아들 해방시켜주기	(자식도 자식이 있을 수 있는 것)
	struct list_elem *elem_orphan;
	struct child_info *orphan;

	while(!list_empty(&curr->child_list)) {			// child_tid 와 같은 tid의 child_info 를 찾음
		elem_orphan = list_pop_front (&curr->child_list);
		orphan = list_entry(elem_orphan, struct child_info, elem_c); 	

		if(orphan->is_zombie) {					
			list_remove(elem_orphan);				// 알아서 죽은(부모의 wait와 별개로 그냥 혼자 죽은 경우) 자식들의 child_info를 child_list에서 뺌
			free(orphan);							// 알아서 죽은(부모의 wait와 별개로 그냥 혼자 죽은 경우) 자식들의 child_info를 free	
		} 
		else { 										// 이제 부모 없을거니까 자기 정보 안알려줘도 되게 만들어줘야함
			list_remove(elem_orphan);				// 아직 살아있는 자식들의 child_info를 child_list에서 뺌
			orphan->child_thread->my_info = NULL;	// 자식의 child_info는 이제 필요없어져서 free 할 것인데, 그 반환된 공간에 자식(자식)이 죽으면서 자신(자식)의 정보를 적으려 접근하지 못하게 포인터를 NULL로 바꿔줌
			free(orphan);	 						// 이제 부모가 없으니까 자식은 자신이 죽을 때 자신의 정보를 적어줄 필요가 없으니까 child_info free
		}		
	}
	if(curr->my_info) { 									// 명시적으로 부모한테 자식(나)의 죽음 정보 알려주기
		curr->my_info->exit_status = curr->exit_status;		// 나(자식)의 exit_status (exit()의 인자로 전달받음) 를 적어 둠
		curr->my_info->is_zombie = true;					// 내(자식)이 이제 좀비가 되었다는 사실을 적어둠	
		sema_up (&curr->my_info->sema);						// 부모가 내(자식)의 흔적을 지울 수 있게 부모를 깨워줌
	}
}

/* Free the current process's resources. */
/* 현재 자원을 깨끗 하게 정리*/
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();
	lock_acquire(&filesys_lock);
	file_close(curr->current_file);
	curr->current_file = NULL;
	lock_release(&filesys_lock);
#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	/* 현재 프로세스의 페이지 디렉토리를 파괴하고 커널 전용 페이지 디렉토리로 다시 전환 */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		/* 여기서 올바른 순서가 중요합니다. 
		타이머 인터럽트가 프로세스 페이지 디렉토리로 다시 전환할 수 없도록 페이지 디렉토리를 전환하기 전에 cur->pagedir을 NULL로 설정해야 합니다. 
		프로세스의 페이지 디렉토리를 파괴하기 전에 기본 페이지 디렉토리를 활성화해야 합니다. 
		그렇지 않으면 활성 페이지 디렉토리가 해제되고 지워진 디렉토리가 됩니다.*/
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
/* 네스트 스레드에서 사용자 코드를 실행하기 위해 CPU를 설정합니다.
이 함수는 모든 컨텍스트 전환에서 호출됩니다.*/
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;	
	uint64_t p_offset;	// file 안에서 우리가 읽을 부분의 시작 위치
	uint64_t p_vaddr;	// VM 상에서의 주소
	uint64_t p_paddr;	// RAM 에서의 주소 (우리가 vaddr을 page table 통해서 변형해서 얻게되는 주소)
	uint64_t p_filesz;	// 실제 읽어야할 data 읽을 크기
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
/* FILE_NAME에서 현재 스레드로 ELF 실행 파일을 로드합니다.
  실행 파일의 진입점을 *RIP에 저장하고 초기 스택 포인터를 *RSP에 저장합니다. 
  성공하면 true, 그렇지 않으면 false를 반환합니다. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());
	
	/* PJT 2 - 
	 * 1. command 를 단어들로 쪼개라
	 * Parse file_name 
	 * strtok_r() 사용
	 */
	char *f_nm, *tmp_ptr;
	char tmp_file_nm[40]; // 파일이름 40자 제한
	strlcpy(tmp_file_nm, file_name, strlen(file_name)+1);
	f_nm = strtok_r(tmp_file_nm, " ", &tmp_ptr);
	
	/* Open executable file. */
	// file = filesys_open (file_name);
	file = filesys_open (f_nm);	// 위에서 active 하였기 때문에 (process_activate()) 이제 실행가능한 file이 되었고 얘를 open
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}


	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr	// file의 headr 읽음 ehdr(ELF header) 
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {			// phnum = program header number
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))	// file_ofs이 file_length 보다 크다 == 더 이상 쓸 공간이 없음, 0보다 작다 == file에 문제
			goto done;
		file_seek (file, file_ofs);	// 커서를 file_ofs까지 이동 시켜줌 -> 그 이후부터 사용하기 위함

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr) // file을 읽어서 phdr에 넣어줌 phdr = program header. phdr에서 읽어낸 size가 phdr size와 같은지 검사
			goto done;
		file_ofs += sizeof phdr;		// file_ofs의 위치를 phdr의 크기만큼 이동해서 다음 내용 읽으려고
		switch (phdr.p_type) {
			case PT_NULL:	// PT 
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {	// PHDR이 file안의 valid 하면서 load 가능한 segment에 대한 내용을 담고 있는지 검사
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;	// file_page를 phdr.p_offest에서 0~11 가 아닌 비트 정보로 초기화, 주소[:12] + offset[0:11] 에서 offset 제외한 부분 = file의 위치 파싱 한 것
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;		// mem_page를 phdr.p_vaddr에서 0~11 가 아닌 비트 정보로 초기화 vaddr = 주소+offset
					uint64_t page_offset = phdr.p_vaddr & PGMASK;	// page_offset를 phdr.p_vaddr에서 0~11 비트 정보로 초기화 vaddr = 주소+offset
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	// 초기 RSP 할당
	if (!setup_stack (if_))
		goto done;

	/* File start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */


	/* 
	1. 리스트 스택 쌓기
	*/
	char *token= NULL; 
	char *save_ptr = NULL;
	int argc = 0;
	int tmp_len;
	void* stack_offset = if_->rsp;
	void* tmp_list_offset;

	for (token = strtok_r (file_name, " ", &save_ptr); token != NULL; token = strtok_r (NULL, " ", &save_ptr)){
		tmp_len = strlen(token)+1;
		stack_offset -= (sizeof(char) * tmp_len ); 
		strlcpy((char *)stack_offset, token, tmp_len);
		argc ++;
	}

	// hex_dump(stack_offset, stack_offset, if_->rsp - (int)stack_offset, true);

	// argv 순회용 주소값 설정
	tmp_list_offset = stack_offset;

	/* 
	2. offset aligin 설정
	*/
	while(((int)stack_offset % 8) != 0){
		stack_offset--;
	}

	/* 
	3. argv, return 주소값 세팅
	*/
	//point주소 저장용 임시변수
	uintptr_t* tmp_point = NULL;
	uintptr_t addr_point;
	
	// 공간 미리 할당 (argc개수 + 2 (argv[argc], return address))
	stack_offset -= (sizeof(uintptr_t) * (argc +2));
	
	// return address 저장
	memset(stack_offset, 0, sizeof(uintptr_t));


	// argv 주소값 저장
	i = 0;
	for (; tmp_list_offset < if_->rsp; tmp_list_offset+=(strlen(tmp_list_offset)+1)){
		addr_point = (uintptr_t *)tmp_list_offset;
		memcpy(stack_offset+sizeof(uintptr_t) * (argc-i), &addr_point , sizeof(uintptr_t));
		i++;
	}


	// argv[argc] 주소값 저장
	tmp_point = NULL;
	memset(stack_offset + sizeof(uintptr_t) * (argc+1), 0, sizeof(uintptr_t));
	
	// 테스트
	// hex_dump(stack_offset, stack_offset, if_->rsp - (int)stack_offset, true);

	/* 
	4. rsi -> argv[0], rdi -> argc 할당, rax값 넣기
	*/
	if_->R.rdi = argc;
	if_->R.rsi = stack_offset+(sizeof(uintptr_t));
	// if_->R.rax = stack_offset;

	// 인터럽트 값 확인
	// intr_dump_frame(if_);

	success = true;
	// RSP 이동
	if_->rsp = stack_offset;


done:
	/* We arrive here whether the load is successful or not. */
	if (!success){
		file_close (file);
	}
	else {
		t->current_file = file;
		file_deny_write(file);
	}
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	size_t page_read_bytes = ((struct args_lazy *)aux)->page_read_bytes;
	size_t page_zero_bytes = ((struct args_lazy *)aux)->page_zero_bytes;
	off_t ofs = ((struct args_lazy *)aux)->ofs;
	struct file* file = ((struct args_lazy *)aux)->file;
	
	file_seek (file, ofs);

	if (file_read (file, page->frame->kva, page_read_bytes) != (int) page_read_bytes) {
		PANIC("TODO : file_read fail - palloc_free_page (page");
		return false;
	}
	memset (page->frame->kva + page_read_bytes, 0, page_zero_bytes);
	
	// free(aux);
	
	return true;
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		struct args_lazy *aux = (struct args_lazy *) malloc (sizeof(struct args_lazy));
		aux->page_read_bytes = page_read_bytes;
		aux->page_zero_bytes = page_zero_bytes;
		aux->ofs = ofs;
		aux->file = file;

		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
											writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += PGSIZE;
	}

	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);
	struct page *new_page = NULL;
	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */
	// #define vm_alloc_page(type, upage, writable) \
	// vm_alloc_page_with_initializer ((type), (upage), (writable), NULL, NULL)
	
	vm_alloc_page(VM_ANON | VM_MARKER_0, stack_bottom, true); // VM_ANON | 마킹 (stack 임)
	
	success = vm_claim_page (stack_bottom);
	
	if (success)
		if_->rsp = USER_STACK;

	return success;
}
#endif /* VM */
