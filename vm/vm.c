/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"


/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */

	// RAM 	 20*2^20 byte
	// frame 수  5*KB * 8byte(list_elem size) ->  40KB : Frame table 크기
	// spt 에 적어주는거 뭔가 여기서 해야하나 ? 아니면 spt_init  ?

}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va ) {
	struct page *page = NULL;
	struct page *e_page;
	/* TODO: Fill this function. */

	for (struct list_elem *e = list_begin (&spt->list_spt); e != list_end (&spt->list_spt); e = list_next (e))
	{	
		e_page = list_entry (e, struct page, elem_spt);
		if (va == e_page->va) {
			page = e_page;
			break;
		}
	}
	return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */

	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	// palloc 하면 userpool or kernel pool에서 가져와 가져온걸 우리가 frame table에서 관리 하게 됨
	frame = palloc_get_page(PAL_USER | PAL_ZERO); // userpool에서 0으로 초기화된 새 frame (page size) 가져옴

	ASSERT (frame != NULL);			// 진짜로 가져왔는지 확인
	ASSERT (frame->page == NULL);   // 어떤 page도 올라가 있지 않아야 함 (빈공간인지 확인)
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	/*
	1. 보조 페이지 테이블에서 폴트가 발생한 페이지를 찾는다.
	-> spt_find_page (구현해야지)
	2. 만일 메모리 참조가 유효하다면 보조 페이지 엔트리를 사용해서 페이지에 들어가는 데이터를 찾는다.
	3. 페이지를 저장하기 위해 프레임을 획득.
	4. 데이터를 파일 시스템이나 스왑에서 읽어오거나, 0으로 초기화하는 등의 방식으로 만들어서 프레임으로 가져옵니다.
	5. 가상주소에 대한 페이지 테이블 엔트리가 물리 페이지를 가리키도록 지정합니다 -> vm_do_claim_page
	*/

	return vm_do_claim_page (page);	// vm (page) -> RAM (frame) 이 연결관계가 없을 때 뜨는게 page fault 이기 때문에 이 관계를 claim 해주는 do_claim 을 호출 해서 문제 해결
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va) { // va랑 page 매핑
	struct page *page = NULL; 	  // 가상 메모리상 stack 영역에 struct page 할당 받고 그 안에는 NULL로 초기화 된 것 
	struct thread *curr = thread_current();
	/* TODO: Fill this function */

	page->va = va;
	// spt_insert_page();

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) { // page <-> frame 매핑
	struct frame *frame = vm_get_frame ();
	struct thread *curr = thread_current();
	bool writable = true;

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if (pml4_set_page(curr->pml4, page->va, frame->kva, writable)){ //pml4_set_page는 유저와 프레임을 매핑
		return false;
	}
	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	list_init(&spt->list_spt);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}
