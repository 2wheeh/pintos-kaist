#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* fd constants */
#define FD_MIN    2
#define FD_MAX    128

/* exit error (temporal) */
#define EXIT_MY_ERROR -1

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int exit_status;		 			// 종료 상태 0~255, -1 ? 
	int priority;                       /* Priority. */
	int wakeup_tick;
	int init_priority;   				// donation 이후 우선순위를 초기화하기 위해 초기값 저장
	
	struct file *fd_array[FD_MAX];
	
	struct file *current_file;
	
	struct lock *wait_on_lock; 		// 해당 스레드가 대기 하고있는 lock자료구조의 주소 저장
	struct list donations; 			// multiple donation 을 고려하기 위해 사용 
	struct list_elem donation_elem; // multiple donation 을 고려하기 위해 사용
	
	struct thread *my_parent;		// 이 쓰레드(자식)가 create되는 순간의 running thread를 저장
	struct child_info *my_info;		// 엄마가 볼 내 정보를 적어둔 구조체의 주소
	struct list child_list;			// list for child (spawned from thread_create, )

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
	void *stack_bottom;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

struct child_info {						// 내가 죽을 때 그 정볼르 명시적으로 남겨서 부모가 후에 볼 수 있게하기 위함
	bool is_zombie;						// 내(자식)가 죽으면 zombie true로 바꿔둘 것임 
	tid_t tid;							// 내(자식)의 tid
	int exit_status;					// 내(자식)가 exit()으로 죽을때 인자로 전달받은 exit status
	struct list_elem elem_c;			// child_info를 list_elem을 사용해서 child_list(연결리스트)로 관리
	struct semaphore sema;				// 자식 죽음기다릴 때 (wait()) 사용할 sema
	struct thread *child_thread;		// 내(자식) 주소
};


/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

bool cmp_priority (struct list_elem *a, struct list_elem *b, void* aux);
bool cmp_priority_dona (struct list_elem *a, struct list_elem *b, void* aux);
bool cmp_priority_waiter (struct list_elem *a, struct list_elem *b, void* aux);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);
void thread_osiete (int64_t ticks);
void thread_sleep (void);
void thread_awake (int64_t ticks);

void update_next_tick_to_awake(int64_t ticks);
int64_t get_next_tick_to_awake(void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

int destruction_req_check (tid_t);

#endif /* threads/thread.h */
