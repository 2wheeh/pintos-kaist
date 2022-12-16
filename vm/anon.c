/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);
	lock_init(&st.lock);
	st.slots_map = bitmap_create(SLOT_MAX_CNT);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	// printf(":::page addr %p anon initializer called:::\n", page);
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;

	anon_page->init    = page->uninit.init;
	anon_page->type    = type;
	anon_page->aux     = page->uninit.aux;
	anon_page->slot_no = SLOT_NAN;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	// printf(":::page addr %p anon swap in:::\n", page);
	struct anon_page *anon_page = &page->anon;
	disk_sector_t slot_no = anon_page->slot_no;

	if (slot_no != SLOT_NAN) {
		read_slot_to_page (slot_no, kva);
		salloc_free_slot (slot_no);
		anon_page->slot_no = SLOT_NAN;
		return true;
	}

	return false;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	// printf(":::page addr %p anon swap out called:::\n", page);

	struct anon_page *anon_page = &page->anon;
	struct frame *frame = page->frame;
	
	disk_sector_t slot_no = salloc_get_slot();

	anon_page->slot_no = slot_no;
	write_page_to_slot(slot_no, frame->kva);
	memset(frame->kva, 0, PGSIZE);

	if(pml4_get_page(page->pml4, page->va))
		pml4_clear_page(page->pml4, page->va);

	page->frame = NULL;

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	// printf(":::page addr %p anon_destory called:::\n", page);


}

/* Return a number of slots in swap_disk */
disk_sector_t
slot_max_cnt (void) {
	return disk_size (swap_disk) / SECTOR_PER_SLOT;
}

disk_sector_t 
salloc_get_slot (void) {
	return salloc_get_multiple(1);
}

disk_sector_t 
salloc_get_multiple (size_t slot_cnt) {
	lock_acquire (&st.lock);
	disk_sector_t slot_no = bitmap_scan_and_flip (st.slots_map, 0, slot_cnt, false);
	lock_release (&st.lock);

	return slot_no;
}

void 
salloc_free_slot (disk_sector_t slot_no) {
	salloc_free_multiple(slot_no, 1);
}

void 
salloc_free_multiple (disk_sector_t slot_no, size_t slot_cnt) {
	lock_acquire (&st.lock);
	bitmap_set_multiple (st.slots_map, slot_no, slot_cnt, false);
	lock_release (&st.lock);
}

void 
write_page_to_slot (disk_sector_t slot_no, void *upage) {
	disk_sector_t sector_no = slot_to_sector(slot_no);

	for (int i =0; i<SECTOR_PER_SLOT; i++) {
		disk_write (swap_disk, sector_no + i, upage);
		upage += DISK_SECTOR_SIZE;
	}
}

void 
read_slot_to_page  (disk_sector_t slot_no, void *upage) {
	disk_sector_t sector_no = slot_to_sector(slot_no);

	for (int i =0; i<SECTOR_PER_SLOT; i++) {
		disk_read (swap_disk, sector_no + i, upage);
		upage += DISK_SECTOR_SIZE;
	}
}