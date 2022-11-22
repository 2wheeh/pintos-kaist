#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "include/lib/user/syscall.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void custom_dump_frame(struct intr_frame *f);

int write_handler (f);

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

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	/* 인터럽트 서비스 루틴은 syscall_entry가 사용자 영역 스택을 커널 모드 스택으로 교체할 때까지 인터럽트를 제공하지 않아야 합니다. 
	따라서 FLAG_FL을 마스킹했습니다.*/
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}


/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	uint64_t syscall_type = f->R.rax;

	switch(syscall_type){
		case SYS_WRITE:
			write_handler(f);
			break;
		case SYS_EXIT:
			thread_exit();
			break;
	}
}

int write_handler(struct intr_frame *f){
	char *save_point;
	int size = 0;
	putbuf(f->R.rsi,f->R.rdx);
}




