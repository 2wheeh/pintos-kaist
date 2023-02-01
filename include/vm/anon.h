#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
#include "devices/disk.h"
#include <bitmap.h>

struct page;
enum vm_type;

// 1 slot   = 1 page    = 4096 bytes
// 1 sector = 512 bytes
// 1 slot   = 8 sectors

#define SLOT_MAX_CNT                 (slot_max_cnt())
#define SECTOR_PER_SLOT              ((PGSIZE) / (DISK_SECTOR_SIZE))
#define SLOT_NAN                     (SLOT_MAX_CNT + 1)
#define sector_to_slot(sector_no)    ((disk_sector_t)((sector_no) / (SECTOR_PER_SLOT)))
#define slot_to_sector(slot_no)      ((disk_sector_t)((slot_no) * (SECTOR_PER_SLOT)))

struct anon_page {
    vm_initializer *init;
	enum vm_type type;
	void *aux;
    disk_sector_t slot_no;
};

struct swap_table {
    struct lock lock;
    struct bitmap *slots_map;
};

struct swap_table st;

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

disk_sector_t slot_max_cnt(void);
disk_sector_t salloc_get_slot (void);
disk_sector_t salloc_get_multiple (size_t slot_cnt);
void salloc_free_slot (disk_sector_t);
void salloc_free_multiple (disk_sector_t, size_t slot_cnt);
void write_page_to_slot (disk_sector_t slot_no, void *upage);
void read_slot_to_page  (disk_sector_t slot_no, void *upage);
#endif
