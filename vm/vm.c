/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "hash.h"


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
	list_init(&frame_table); //
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
 * `vm_alloc_page`.
 * 이 함수는 새로운 페이지를 할당받고 초기화하는 작업을 한다.  */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {
	ASSERT (VM_TYPE(type) != VM_UNINIT)
	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not.*/
	if (spt_find_page (spt, upage) == NULL) { //!말록이 페이지 사이즈만큼의 메모리 공간을 new_page라면서 뱉었어. 근데 그 페이지가 spt_table에 이미 있는거면 안된다는 소리임. spt테이블에 이미 있다는게 뭐길래?
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		struct page *new_page = (struct page *) malloc(sizeof(struct page));
		bool (*initializer)(struct page*, enum vm_type, void *); //함수포인터 initializer를 정의

		if(new_page == NULL){
			goto err;}
		switch (VM_TYPE(type)){ //!VM_TYPE으로 안하고 그냥 type하면 default 쪽 vm_alloc_initializer fail 에러 발생
			case VM_ANON:
				initializer = anon_initializer; //함수포인터 initializer에 함수를 담음. 그리고 실행시키려면 initializer()해주면 실행됨.
				break;
			case VM_FILE:
				initializer = file_backed_initializer;
				break;
			default:
				PANIC("vm_alloc_initializer fail");
		}
		uninit_new(new_page, upage, init, type, aux, initializer);  //uninit_new를 하면 uninit page가 만들어짐.
		bool spt_judge = spt_insert_page(spt,new_page);
		// printf("!!!!!!!!!!!!!!!!!%d" , spt_judge);
		/* TODO: Insert the page into the spt. */
		return spt_judge; //이 작업을 다 한 뒤에야 spt에 넣는다.
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL.
어떤 주소 va를 줄테니 spt테이블에서 va랑 매핑된 page를 찾아라 */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va ) {
	// struct page *page = NULL;
	/* TODO: Fill this function. */
	
	struct page *page = (struct page*) malloc(sizeof(struct page)); //page를 만들고
	struct hash_elem *e;
	page->va = pg_round_down(va); 					 //인자로 받은 va가 속해있는 페이지의 시작주소를 pg_round_down(va)로 구하고, 새로만든 page의 va가 pg_round_down을 가리키게 한다.
	e = hash_find(&spt->spt_hash, &page->hash_elem); //spt해시테이블에서 &page->hash_elem을 찾는다. 있으면 spt 해시테이블에 있는 &page->hash_elem을 반환
	free(page);

	return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL; //해시테이블에 있는 elem이면 그 elem이 속한 page를 리턴 
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	// int succ = false;
	/* TODO: Fill this function. */

	return page_insert(&spt->spt_hash, page); //spt해시함수에 page의 elem구조체 삽입
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
	//먼저들어온 순서대로 렘에서 퇴출
	victim = list_entry(list_pop_front(&frame_table), struct frame, frame_elem);
	return victim;
}



/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) { //palloc으로 새로운 페이지를 요청했는데 물리메모리가 꽉 차서 줄 수 없을 때 기존에 올라온 것들 중 하나를 퇴출시켜 공간을 확보해 줘야 한다.
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	swap_out(victim->page);
	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	// struct frame *frame = NULL;  //이렇게 선언하면 커널스택에 *frame이라는 포인터(주소)의 공간을 만든거지 실제 frame을 위한 공간을 만든건 아니다. 
									//따라서 말록으로 힙 공간에 실제 frame을 만들어주고 그 공간을 *frame이 가리키도록 한다.
	struct frame *frame = (struct frame *) malloc(sizeof(struct frame)); //!Free해줘야해

	/* TODO: Fill this function. */
	// palloc 하면 userpool or kernel pool에서 가져와 가져온걸 우리가 frame table에서 관리 하게 됨
	frame->page = NULL; 			//frame페이지 받아온거 NULL로 초기화

	ASSERT (frame != NULL);			// frame은 NULL이 아니어야해! (진짜로 가져왔는지 확인)
	ASSERT (frame->page == NULL);   // 새로 받았으니까 frame에는 어떤 page도 쓰레기 값으로 올라가 있지 않아야 함 (빈공간인지 확인)
	
	frame->kva = palloc_get_page(PAL_USER | PAL_ZERO); // 물리메모리 userpool에서 0으로 초기화된 새 frame (page size) 가져옴
	if(frame->kva == NULL){ 		// palloc으로 유저풀에서 물리램 받아오려고 했는데 NULL이다? = 유저풀에서 줄 공간이 없다 ㅠㅠ
		frame = vm_evict_frame();	// 그럼 evict하여 frame 공간을 확보한다.
		frame->page = NULL; 		// evict해서 받아온 여유공간을 NULL로 초기화
		return frame;
	}else{
		list_push_back(&frame_table, &frame->frame_elem); //프레임테이블에 방금 물리램 유저풀에서 받아온 따끈한 frame을 집어넣음.
		frame->page = NULL;
		return frame;
	}
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
vm_try_handle_fault (struct intr_frame *f , void *addr,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	addr = pg_round_down(addr);
	page=spt_find_page(spt, addr);
	printf("!!!!!!!!!!addr %p, !!!!!!!!!!page %p !!!!!!!!!!!size of page %X\n", addr, page, sizeof(page));
	// PANIC("pages!!!! %X", page);
	if(page==NULL){
		PANIC("real_page_fault");
	}else{
		return vm_do_claim_page (page);	// vm (page) -> RAM (frame) 이 연결관계가 없을 때 뜨는게 page fault 이기 때문에 이 관계를 claim 해주는 do_claim 을 호출 해서 문제 해결
	}

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
vm_claim_page (void *va) { 				// 인자로 주어진 va에 페이지를 할당하는 역할. 이후 do_claim을 호출해서 페이지랑 프레임이랑 연결
	struct page *page = NULL; 	  
	struct thread *curr = thread_current();
	/* TODO: Fill this function */

	page = spt_find_page(&curr->spt, va);
	if (!page) PANIC("claim panic");
	
	page->va = va;

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) { 		// 가상메모리의 page와 물리메모리의 frame이 잘 매핑되었나 체크
	struct frame *frame = vm_get_frame ();
	struct thread *curr = thread_current();
	bool writable = true;

	/* Set links */							// 인자로 받은 page와 get_frame으로 받은 새 Frame과 연결
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	
											//pml4_set_page는 유저페이지와 프레임을 매핑(매핑되면 true반환)
	if (!pml4_set_page(curr->pml4, page->va, frame->kva, writable)){ 
		return false;
	}
	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table
spt를 초기화하는 함수. 페이지폴트가 뜰 때마다 spt를 조회해야하니 빠른 탐색이 가능한 hash자료구조로 설정 예정
 */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->spt_hash, page_hash, page_less,NULL);
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

// 해시테이블에 인자로 받은 페이지의 elem을 삽입하는 함수.
bool page_insert(struct hash *h, struct page *p){
	
	if( !hash_insert(h, &p->hash_elem)){ //뭔가를 리턴 받은게 있으면? true
		return true;
	}else{								 //없으면 false
		return false;
	}
}

// 해시테이블에 인자로 받은 페이지의 elem을 지우는 함수.
bool page_delete(struct hash *h, struct page*p){
	if(!hash_delete(h, &p->hash_elem)){
		return true;
	}else{
		return false;
	}
}
