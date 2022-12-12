/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

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
	struct file_page *file_page UNUSED = &page->file;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	uint32_t read_bytes;
	uint32_t zero_bytes;
	size_t filesize = file_length(file);

	if (filesize < length) {
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

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		struct args_lazy *aux = (struct args_lazy *) malloc (sizeof(struct args_lazy));
		*aux = (struct args_lazy) { 
				.page_read_bytes = page_read_bytes,
				.page_zero_bytes = page_zero_bytes,
				.ofs = offset,
				.file = file,
		};

		if (!vm_alloc_page_with_initializer (VM_FILE, addr,
											writable, lazy_load_segment_mmap, aux))
			return NULL;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += PGSIZE;
	}
	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {

}

static bool
lazy_load_segment_mmap (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	size_t page_read_bytes = ((struct args_lazy *)aux)->page_read_bytes;
	size_t page_zero_bytes = ((struct args_lazy *)aux)->page_zero_bytes;
	off_t ofs = ((struct args_lazy *)aux)->ofs;
	struct file* file = ((struct args_lazy *)aux)->file;
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