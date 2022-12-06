/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "userprog/process.h"

/* project for 3 - start */
struct list frame_table;
/* project for 3 - end */

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
	list_init(&frame_table);

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

		struct page *new_page = (struct page *)malloc(sizeof(struct page));
		bool (*initializer)(struct page*, enum vm_type, void *);

		if(new_page ==NULL)
			goto err;
		
		switch (type){
			case VM_ANON :
				initializer = anon_initializer;
				break;
			case VM_FILE :
				initializer = file_backed_initializer;
				break;
			default:
				PANIC("vm_alloc_initializer fail");
		}
		uninit_new(new_page, upage, init, type, aux, initializer);

		new_page->writable = writable;
		/* TODO: Insert the page into the spt. */
		return spt_insert_page(spt,new_page);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va ) {
	struct page *page = (struct page *)malloc(sizeof(struct page));
	struct page *e_page;
	struct hash_elem *e;
	/* TODO: Fill this function. */

	page->va = pg_round_down(va);
	e=hash_find(&spt->pages, &page->elem_hash);

	free(page);

	if (e != NULL)
		page = hash_entry(e, struct page, elem_hash);
	else
		page = NULL;

	// for (struct list_elem *e = list_begin (&spt->list_spt); e != list_end (&spt->list_spt); e = list_next (e))
	// {	
	// 	e_page = list_entry (e, struct page, elem_spt);
	// 	if (va == e_page->va) {
	// 		page = e_page;
	// 		break;
	// 	}
	// }

	return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */

	return insert_page(&spt->pages, page);
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

	struct thread *cur = thread_current();
	struct list_elem *start, *end;


	/*내 생각은 하나면 될거 같은데??*/
	for (start= list_begin(&frame_table); start != list_end(&frame_table); start=list_next(start)){
		victim = list_entry(start, struct frame, elem_fr);
		if(pml4_is_accessed(cur->pml4,victim->page->va))
			pml4_set_accessed(cur->pml4,victim->page->va, 0);
		else
			return victim;
	}

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim (); //  struct frame을 받아서 그 안에 내용을 다 지워주는 함수
	/* TODO: swap out the victim and return the evicted frame. */
	swap_out(victim->page);

	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
	/* TODO: Fill this function. */
	// palloc 하면 userpool or kernel pool에서 가져와 가져온걸 우리가 frame table에서 관리 하게 됨

	ASSERT (frame != NULL);			// 진짜로 가져왔는지 확인
	ASSERT (frame->page == NULL);   // 어떤 page도 올라가 있지 않아야 함 (빈공간인지 확인)

	frame->kva = palloc_get_page(PAL_USER); // userpool에서 0으로 초기화된 새 frame (page size) 가져옴
	if(frame->kva == NULL){ // 만약에 frame이 비어있다면
		frame = vm_evict_frame(); // 해당 page를 삭제하고 frame을 반환함
		frame->page =NULL; // page 주소도 NULL로 바꾸고
		return frame; // frame을 return
	}
	list_push_back(&frame_table, &frame->elem_fr);
	frame->page = NULL;

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

	// page->va = va;
	// spt_insert_page();
	page = spt_find_page(&curr->spt,va);
	if(page ==NULL)
		return false;
	

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
	// if (!pml4_set_page(curr->pml4, page->va, frame->kva, writable)) {
	// 	return false;
	// }
	if(install_page(page->va,frame->kva,page->writable)){
		return swap_in (page, frame->kva);
	}

	return false;
	
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	list_init(&spt->list_spt);
	hash_init(&spt->pages,page_hash,page_comp_less, NULL); // hash를 사용하기 위해서 초기화
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

// hash를 사용하기 위한 함수 -1
// hash에서 받은 page에서 size 크기만큼의 hash를 반환함
unsigned page_hash(const struct hash_elem *p_, void *aux UNUSED){
	const struct page *p = hash_entry(p_, struct page, elem_hash);
	return hash_bytes(&p->va, sizeof(p->va));
}

// hash를 사용하기 위한 함수 -2
// input 된 두개의 hash 크기를 비교함
bool page_comp_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED){
	const struct page *a = hash_entry(a_, struct page, elem_hash);
	const struct page *b = hash_entry(b_, struct page, elem_hash);

	return a->va < b->va;
}


// hash에 page를 insert하는 함수 - 성공하면 return true fail -> return false
bool insert_page(struct hash *pages, struct page *p){
	if(!hash_insert(pages, &p->elem_hash))
		return true;
	else
		return false;
}
// hash에 page를 delete하는 함수 - 성공하면 return true fail -> return false
bool del_page(struct hash *pages, struct page *p){
	if(!hash_delete(pages, &p->elem_hash))
		return true;
	else
		return false;
}