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

//3주차
struct bitmap *swap_table;
const size_t SECTORS_PER_PAGE = PGSIZE / DISK_SECTOR_SIZE; 

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	
	//swap_disk설정중. swap_disk란 파일 기반 페이지처럼 백업저장소가 없는 익명페이지의 스와핑을 지원하기 위해 디스크에 만든 임시 백업 저장소이다.
	swap_disk = disk_get(1,1); 
	
	//이 기능에서는 스왑 디스크를 설정해야 합니다. 또한 스왑 디스크에서 사용 가능한 영역과 사용된 영역을 관리하기 위한 데이터 구조가 필요합니다. 스왑 영역도 PGSIZE(4096바이트) 단위로 관리됩니다.
	size_t swap_size = disk_size(swap_disk) / SECTORS_PER_PAGE;
	swap_table = bitmap_create(swap_size); //비트들을 전부 0 -> 사용하고 있지 않음으로 바꾸는 함수(예를들어 swap_size가 4비트인데 1010이라는 비트맵이 있으면 0번,2번은 사용가능, 1,3은 사용중임을 의미함)
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
}

/* Swap in the page by read contents from the swap disk.
스왑 디스크 데이터 내용을 읽어서 익명 페이지를(디스크에서 메모리로)  swap in합니다. 스왑 아웃 될 때 페이지 구조는 스왑 디스크에 저장되어 있어야 합니다. 
스왑 테이블을 업데이트해야 합니다(스왑 테이블 관리 참조). */
static bool
anon_swap_in (struct page *page, void *kva) {

}

/* Swap out the page by writing contents to the swap disk.
렘이 꽉 차있어서 특정 프레임을 디스크의 스왑영역으로 쫓아내는 상황.*/
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	// swap table에서 page를 할당받을 수 있는 swap slot 찾기 (스왑테이블을 돌면서 0인비트를 찾으면 될 듯?)
	int page_no = bitmap_scan(swap_table, 0,1,false);
	
	//비트맵스캔으로 뒤졌더니 들어갈 공간이 있으면 그 인덱스를 page_no으로 준다. 없으면 BITMAP_ERROR를 반환
	if(page_no ==BITMAP_ERROR){
		return false;
	}

	//스왑테이블에서 찾은 빈공간에 페이지를 옮기는 작업(쓰는 작업) - 코드가 복잡해서 이정도로만 이해하자..
	for(int i=0; i<SECTORS_PER_PAGE; ++i){
		// 두번째인자에 해당하는 섹터를 첫번째 인자에 해당하는 디스크에 쓴다. 세번째인자에서 두번째인자
		disk_write(swap_disk, page_no * SECTORS_PER_PAGE +i, page->va+DISK_SECTOR_SIZE*i);
	}

	//스왑영역에 썼으니까 bitmap_set으로 해당 slot이 false상태에서 true로 바뀌었음을 마킹해준다.
	bitmap_set(swap_table, page_no, true);

	//스왑영역에 페이지가 채워졋으니 물리프레임과 매핑되어있는 페이지를 지워준다.(evit완료)
    //이제 프로세스가 이 페이지에 접근하면 page fault가 뜬다.
	pml4_clear_page(thread_current()->pml4, page->va);

	//anon페이지에게 쫓아내서 미안..너가 스왑영역에 저장되어있는 주소는 이거야~ 나중에 디스크에서 page_no뒤지면 됨 하고 알려준다.
	anon_page->swap_index = page_no;
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}
