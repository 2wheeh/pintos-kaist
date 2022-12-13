/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);
static bool file_backed_write_back (void *aux, void *kva);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	page->file.init = page->uninit.init;
	page->file.type = page->uninit.type;
	page->file.aux  = page->uninit.aux;

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page = &page->file;
	struct frame *frame = page->frame;
	struct args_lazy_mm *aux = file_page->aux;
	unsigned *mmap_cnt = aux->mmap_cnt;
	bool filesys_lock_taken_here = false;

	if ( !lock_held_by_current_thread(&filesys_lock))
	{
		lock_acquire(&filesys_lock);
		filesys_lock_taken_here = true;
	}

	 if(pml4_is_dirty(thread_current()->pml4, page->va)) {
        file_backed_write_back((void *)aux, frame->kva);
    }

	*mmap_cnt -= 1;
	// reopen 한 file이 더 이상 mmap 된 곳 없으면 close
	if (*mmap_cnt == 0)
	{	

		file_close(aux->file);
		free(mmap_cnt);
		aux->mmap_cnt = NULL;

	}
	if (filesys_lock_taken_here) lock_release(&filesys_lock);

	free(aux);
	file_page->aux = NULL;

	memset(frame->kva, 0, PGSIZE);
	pml4_clear_page(thread_current()->pml4, page->va);

	page->frame->page = NULL;
	page->frame = NULL;

	// TODO : frame_table에서 frame 제거 

	palloc_free_page(frame->kva);
	free(frame);

}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	uint32_t read_bytes;
	uint32_t zero_bytes;
	size_t filesize = file_length(file);
	unsigned *mmap_cnt = (unsigned *) malloc(sizeof(unsigned));
	void *given_addr = addr;

	if (filesize < length) { // file 의 실제 size가 user가 원하는 length 보다 작은 경우 
		read_bytes = filesize;
		zero_bytes = pg_round_up(length) - read_bytes;
	}
	else 
	{
		read_bytes = length;
		zero_bytes = pg_round_up(read_bytes) - read_bytes;
	}

	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (addr) == 0);
	ASSERT (offset % PGSIZE == 0);

	*mmap_cnt = (zero_bytes + read_bytes) / PGSIZE;

	lock_acquire(&filesys_lock);
	file = file_reopen(file);
	lock_release(&filesys_lock);
	
	while (read_bytes > 0 || zero_bytes > 0) {
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct args_lazy_mm *aux = (struct args_lazy_mm *) malloc (sizeof(struct args_lazy_mm));
		*aux = (struct args_lazy_mm) { 
				.page_read_bytes = page_read_bytes,
				.page_zero_bytes = page_zero_bytes,
				.ofs = offset,
				.file = file,
				.mmap_cnt = mmap_cnt,
		};

		if (!vm_alloc_page_with_initializer (VM_FILE, addr,
											writable, lazy_load_segment_mmap, aux))
			return NULL;

		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += PGSIZE;
	}

	return given_addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct page *e_page = spt_find_page(&thread_current()->spt, addr);
	struct args_lazy_mm *aux = e_page->file.aux;
	unsigned *mmap_cnt = aux->mmap_cnt;

	while (e_page && *mmap_cnt) {
		spt_remove_page(&thread_current()->spt, e_page);
		addr += PGSIZE;
		e_page = spt_find_page(&thread_current()->spt, addr);
	}
}

static bool
lazy_load_segment_mmap (struct page *page, void *aux) {
	size_t page_read_bytes = ((struct args_lazy_mm *)aux)->page_read_bytes;
	size_t page_zero_bytes = ((struct args_lazy_mm *)aux)->page_zero_bytes;
	off_t ofs = ((struct args_lazy_mm *)aux)->ofs;
	struct file* file = ((struct args_lazy_mm *)aux)->file;
	bool filesys_lock_taken_here = false;

	if ( !lock_held_by_current_thread(&filesys_lock))
	{
		lock_acquire(&filesys_lock);
		filesys_lock_taken_here = true;
	}

	file_seek (file, ofs);

	size_t read_result;

	if ((read_result = file_read (file, page->frame->kva, page_read_bytes)) != (int) page_read_bytes) {
		// PANIC("TODO : file_read fail 하면 palloc_free_page \n");
		return false;
	}
	memset (page->frame->kva + page_read_bytes, 0, page_zero_bytes);

	if (filesys_lock_taken_here) lock_release(&filesys_lock);

	return true;
}

static bool
file_backed_write_back(void *aux, void *kva) {
	struct args_lazy_mm *args = (struct args_lazy_mm *)aux;
	struct file *file_ptr = args->file;
	off_t offset = args->ofs;
	size_t read_bytes = args->page_read_bytes;

	file_seek(file_ptr, offset);
	file_write(file_ptr, kva, read_bytes);
}