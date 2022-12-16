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

	// frame_table (hash) init
	// hash_init(&ft.frames, frame_hash, frame_less, NULL);
	// lock_init(&ft.lock);
	frame_table_init();
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
	
	/* Check whether the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		struct page *new_page = (struct page *) malloc(sizeof(struct page));
		bool (*initializer)(struct page *, enum vm_type, void *);

		if (!new_page) 
			goto err;

		switch (VM_TYPE(type)) {
			case VM_ANON :
				initializer = anon_initializer;
				break;
			case VM_FILE :
				initializer = file_backed_initializer;
				break;
			default :
				PANIC("TODO : type no %d = not supported YET !", type);
		}

		uninit_new(new_page, upage, init, type, aux, initializer);
		new_page->writable = writable;
		new_page->pml4 = thread_current()->pml4;

		/* TODO: Insert the page into the spt. */
		return spt_insert_page(spt, new_page);

	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va ) {
	struct page *page = NULL;
	struct page e_page;
	/* TODO: Fill this function. */

	struct hash_elem *e;
	e_page.va = pg_round_down(va);
	e = hash_find (&spt->pages, &e_page.elem_spt);
	page = e != NULL ? hash_entry (e, struct page, elem_spt) : NULL;

	return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt, struct page *page ) {
	int succ = false;
	/* TODO: Fill this function. */
	
	if(hash_insert(&spt->pages, &page->elem_spt) == NULL) {
		succ = true;
	    ASSERT(spt_find_page(spt, page->va) == page);
	}

	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	hash_delete (&spt->pages, &page->elem_spt);
	
	lock_acquire(&ft.lock);
	vm_dealloc_page (page);
	lock_release(&ft.lock);
	
	return true;
}

/* Get the struct frame, that will be evicted. */
//! EVICTION POLICY (ref. clock algorithm)
//! 1st  free           : frame->page == NULL
//! 2nd  never accessed : frame->page != accessed
//! 3rd  not dirty      : frame->page != dirty
//! last random
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	struct supplemental_page_table *spt = &thread_current()->spt;
	/* TODO: The policy for eviction is up to you. */
   	struct frame *e_frame;
	struct hash_iterator i;
	struct page *e_page = NULL;
	uint64_t *e_pml4 = NULL;

	bool   is_free      = false;
	bool   is_never_acs = false;
	bool   is_not_dirty = false;

	hash_first (&i, &ft.frames);
	while (hash_next (&i))
	{
		e_frame = hash_entry (hash_cur (&i), struct frame, elem_ft);

		if ((!is_free)) 
		{	
			e_page = e_frame->page;
			// 1st victim check
			if(!(e_page)) {	
				victim = e_frame;
				// printf(":::1st victim page addr %p:::\n", victim->page);
				is_free = true;
			} 
			else if (!is_never_acs) 
			{	
				e_pml4 = e_page->pml4; 
				// 2nd victim check
				if(!pml4_is_accessed(e_pml4, e_page->va)){
					victim = e_frame;
					// printf(":::2nd victim page addr %p:::\n", victim->page);
					is_never_acs = true;
				} 
				else if(!is_not_dirty) 
				{	
					// 3rd victim check
					if(!pml4_is_dirty(e_pml4, e_page->va)) {
						victim = e_frame;
						// printf(":::3rd victim page addr %p:::\n", victim->page);
						is_not_dirty = true;
					}
					else if (!victim) victim = e_frame;
				}
			} 
		}

		if(e_page) pml4_set_accessed(e_pml4, e_page->va, false);
	}

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim ();
	ASSERT(victim != NULL);
	// printf(":::victim found:::\n");
	/* TODO: swap out the victim and return the evicted frame. */
	if(victim->page != NULL) {
		// printf(":::type %d::\n", page_get_type(victim->page));
		swap_out(victim->page);
	}
	// printf(":::swapped out:::\n");
	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = (struct frame *)malloc( sizeof(struct frame));
	/* TODO: Fill this function. */
	// palloc 하면 userpool or kernel pool에서 가져와 가져온걸 우리가 frame table에서 관리 하게 됨

	if (frame == NULL) goto err;
	
	frame->kva = palloc_get_page(PAL_USER | PAL_ZERO); // userpool에서 0으로 초기화된 새 frame (page size) 가져옴
	
	if (frame->kva == NULL) {
		free(frame);
		frame = vm_evict_frame();
	} else {
		ft_insert_frame(frame);
	}

	frame->page = NULL;

	ASSERT (frame != NULL);			// 진짜로 가져왔는지 확인
	ASSERT (frame->page == NULL);   // 어떤 page도 올라가 있지 않아야 함 (빈공간인지 확인)
	
	return frame;
err:
	PANIC("TODO: fail to get_frame");
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr) {
	struct supplemental_page_tagle *spt = &thread_current()->spt;

	addr = pg_round_down(addr);
	while (spt_find_page(spt, addr) == NULL) {
		vm_alloc_page(VM_ANON | VM_IS_STACK, addr, true);
		vm_claim_page (addr);
		addr += PGSIZE;
	}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f, void *addr,
		bool user UNUSED, bool write, bool not_present) {
	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page = NULL;

	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	
	if ((!not_present) && write) {
		return false;
	}

	// 1. addr <= stack_bottom
	// 2. addr >= stack_limit (user_stack - 1MB) 
	// 1,2 => user stack 영역에서의 fault임
	//        rsp -8 (return address) <= addr 이라면 옳은 fault -> growth 해주면 됨
	if (USER_STACK_LIMIT < addr && addr <= USER_STACK) { 
		if (f->rsp - 8 <= addr) {
			vm_stack_growth(addr); 
			return true;
		}
		else return false;
	}

	page = spt_find_page(spt, addr);
	if (page == NULL) return false;

	lock_acquire(&ft.lock);
	bool success = vm_do_claim_page (page);
	lock_release(&ft.lock);
	
	return success;	// vm (page) -> RAM (frame) 이 연결관계가 없을 때 뜨는게 page fault 이기 때문에 이 관계를 claim 해주는 do_claim 을 호출 해서 문제 해결
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
vm_do_claim_page (struct page *page) { // page <-> frame 매핑
	// printf(":::page addr %p do claim called:::\n", page);

	if(page->va == 0xabae20) printf(":::1206:::\n");

	struct frame *frame = vm_get_frame ();
	struct thread *curr = thread_current();
	bool writable = page->writable;

	/* Set links */
	frame->page = page;
	page->frame = frame;
	
	// printf(":::page addr %p get frame done:::\n", page);
	// printf(":::page addr %p type = %d:::\n", page, page_get_type(page));

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if (!pml4_set_page(curr->pml4, page->va, frame->kva, writable)) {
		return false;
	}

	return swap_in (page, frame->kva); // 장래희망 실현 (uninit -> anon, file ..)
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	// list_init(&spt->list_spt);
	hash_init(&spt->pages, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src) {
	bool success;
	struct args_lazy *child_aux;
	struct args_lazy *parent_aux;
    struct hash_iterator i;
    
	hash_first (&i, &src->pages);

    while (hash_next (&i)) 
    {
		struct page *parent_page = hash_entry (hash_cur (&i), struct page, elem_spt);
		if (parent_page == NULL) goto err;

		if (page_get_type(parent_page) == VM_FILE) continue;

		if ((parent_aux = parent_page->uninit.aux) != NULL) {

			child_aux = (struct args_lazy *) malloc (sizeof (struct args_lazy));
			if (child_aux == NULL) goto err;

			child_aux->file = parent_aux->file;
			child_aux->ofs  = parent_aux->ofs;
			child_aux->page_read_bytes = parent_aux->page_read_bytes;
			child_aux->page_zero_bytes = parent_aux->page_zero_bytes;
		} else {
			child_aux = NULL;
		}
		 
		if (!vm_alloc_page_with_initializer (page_get_type(parent_page), parent_page->va, parent_page->writable, parent_page->uninit.init, (void *)child_aux))
			goto err;

		if (parent_page->frame) {
			if(!vm_do_claim_page(spt_find_page(dst, parent_page->va))) goto err;
			memcpy(spt_find_page(dst, parent_page->va)->frame->kva, parent_page->frame->kva, PGSIZE);
		}
    }
	return true;

err:
	return false;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	// hash_clear (&spt->pages, spt_destructor);
	hash_destroy (&spt->pages, spt_destructor);
}

void
spt_destructor(struct hash_elem *e, void *aux UNUSED) {
	const struct page *e_page = hash_entry (e, struct page, elem_spt);
	
	lock_acquire(&ft.lock);
	vm_dealloc_page(e_page);
	lock_release(&ft.lock);
}


/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry (p_, struct page, elem_spt);
  return hash_bytes (&p->va, sizeof p->va);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct page *a = hash_entry (a_, struct page, elem_spt);
  const struct page *b = hash_entry (b_, struct page, elem_spt);

  return a->va < b->va;
}

bool frame_table_init(void) {

	lock_init(&ft.lock);

	return hash_init(&ft.frames, frame_hash, frame_less, NULL);
}
void frame_table_kill(void) {
	hash_destroy (&ft.frames, ft_destructor);
}

struct frame *ft_find_frame(void *kva) {
	struct frame *frame = NULL;
	struct frame e_frame;
 
	struct hash_elem *e;
	e_frame.kva = pg_round_down(kva);
	e = hash_find (&ft.frames, &e_frame.elem_ft);
	frame = e != NULL ? hash_entry (e, struct frame, elem_ft) : NULL;

	return frame;
}

bool ft_insert_frame(struct frame *frame) {
	int succ = false;
	
	if(hash_insert(&ft.frames, &frame->elem_ft) == NULL) {
		succ = true;
	    ASSERT(ft_find_frame(frame->kva) == frame);
	}

	return succ;
}

void ft_remove_frame(struct frame *frame) {
	hash_delete (&ft.frames, &frame->elem_ft);
	// dealloc_frame
	return true;
}

/* Returns a hash value for frame f. */
unsigned
frame_hash (const struct hash_elem *f_, void *aux UNUSED) {
  const struct frame *f = hash_entry (f_, struct frame, elem_ft);
  return hash_bytes (&f->kva, sizeof f->kva);
}

/* Returns true if frame a precedes frame b. */
bool
frame_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct frame *a = hash_entry (a_, struct frame, elem_ft);
  const struct frame *b = hash_entry (b_, struct frame, elem_ft);

  return a->kva < b->kva;
}

void
ft_destructor(struct hash_elem *e, void *aux UNUSED) {
	const struct page *e_frame = hash_entry (e, struct frame, elem_ft);
	// vm_dealloc_framee(e_frame);
}