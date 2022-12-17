#include "filesys/fat.h"
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "bitmap.h"

//4주차추가
static struct bitmap *fat_bitmap;  
#define BITMAP_ERROR SIZE_MAX

/* Should be less than DISK_SECTOR_SIZE
 */

/* https://sunset-asparagus-5c5.notion.site/Step-1-fat-c-c729a0a9529d4e64bf90770ce508faf4

FAT Filesystem(디스크)

fat_start = 1
total_sectors = 8
fat_sectors = 3
data_start = 4
fat_length = 4 -> FAT table의 인덱스 개수

				+-------+-------+-------+-------+-------+-------+-------+-------+
				|Boot   |       FAT Table       |          Data Blocks          |
		        |Sector |       |       |       |       |       |       |       |
				+-------+-------+-------+-------+-------+-------+-------+-------+
sector :        0       1       2       3       4       5       6       7     
cluster:                                        1       2       3       4
*/
struct fat_boot {
	unsigned int magic;
	unsigned int sectors_per_cluster; /* Fixed to 1, 하나의 섹터에 한 클러스터 1:1 */
	unsigned int total_sectors; //총 섹터의 수 8개
	unsigned int fat_start; //부트영역 끝나는 부분=fat시작부분 (위 그림의 섹터 1개)
	unsigned int fat_sectors; /* Size of FAT in sectors. */
	unsigned int root_dir_cluster;
};

/* FAT FS 
FAT파일 시스템 정보를 담고 있는 구조체*/
struct fat_fs {
	struct fat_boot bs;
	unsigned int *fat;
	unsigned int fat_length;
	disk_sector_t data_start;
	cluster_t last_clst;
	struct lock write_lock;
};

static struct fat_fs *fat_fs;

void fat_boot_create (void);
void fat_fs_init (void);

/*fat 테이블 초기화*/
void fat_init (void) {
	//FAT 담을 공간을 힙에 할당
	fat_fs = calloc (1, sizeof (struct fat_fs));
	if (fat_fs == NULL)
		PANIC ("FAT init failed");

	// Read boot sector from the disk -> 디스크에서 FAT읽어서 calloc담은 공간에 저장
	unsigned int *bounce = malloc (DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT init failed");
	disk_read (filesys_disk, FAT_BOOT_SECTOR, bounce);
	memcpy (&fat_fs->bs, bounce, sizeof (fat_fs->bs));
	free (bounce);

	// Extract FAT info
	if (fat_fs->bs.magic != FAT_MAGIC)
		fat_boot_create ();
	fat_fs_init ();

	/* Project 4-1: FAT */
	fat_bitmap = bitmap_create(fat_fs->fat_length); // 
}

void
fat_open (void) {
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT load failed");

	// Load FAT directly from the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_read = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_read;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_read (filesys_disk, fat_fs->bs.fat_start + i,
			           buffer + bytes_read);
			bytes_read += DISK_SECTOR_SIZE;
		} else {
			uint8_t *bounce = malloc (DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT load failed");
			disk_read (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			memcpy (buffer + bytes_read, bounce, bytes_left);
			bytes_read += bytes_left;
			free (bounce);
		}
	}
}

void
fat_close (void) {
	// Write FAT boot sector
	uint8_t *bounce = calloc (1, DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT close failed");
	memcpy (bounce, &fat_fs->bs, sizeof (fat_fs->bs));
	disk_write (filesys_disk, FAT_BOOT_SECTOR, bounce);
	free (bounce);

	// Write FAT directly to the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_wrote = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_wrote;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_write (filesys_disk, fat_fs->bs.fat_start + i,
			            buffer + bytes_wrote);
			bytes_wrote += DISK_SECTOR_SIZE;
		} else {
			bounce = calloc (1, DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT close failed");
			memcpy (bounce, buffer + bytes_wrote, bytes_left);
			disk_write (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			bytes_wrote += bytes_left;
			free (bounce);
		}
	}
}

void
fat_create (void) {
	// Create FAT boot
	fat_boot_create ();
	fat_fs_init ();

	// Create FAT table
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT creation failed");

	// Set up ROOT_DIR_CLST
	fat_put (ROOT_DIR_CLUSTER, EOChain);

	// Fill up ROOT_DIR_CLUSTER region with 0
	uint8_t *buf = calloc (1, DISK_SECTOR_SIZE);
	if (buf == NULL)
		PANIC ("FAT create failed due to OOM");
	disk_write (filesys_disk, cluster_to_sector (ROOT_DIR_CLUSTER), buf);
	free (buf);
}

void
fat_boot_create (void) {
	unsigned int fat_sectors =
	    (disk_size (filesys_disk) - 1)
	    / (DISK_SECTOR_SIZE / sizeof (cluster_t) * SECTORS_PER_CLUSTER + 1) + 1;
	fat_fs->bs = (struct fat_boot){
	    .magic = FAT_MAGIC,
	    .sectors_per_cluster = SECTORS_PER_CLUSTER,
	    .total_sectors = disk_size (filesys_disk),
	    .fat_start = 1,
	    .fat_sectors = fat_sectors,
	    .root_dir_cluster = ROOT_DIR_CLUSTER,
	};
}


void
fat_fs_init (void) {
	/* TODO: Your code goes here. */
	ASSERT(SECTORS_PER_CLUSTER ==1);

	/*파일시스템에서 fat가 시작하는 부분 + fat크기만큼 더하면 그 다음부터는 데이터 영역이 시작(혼고컴운 463그림참고)*/
	fat_fs->data_start = fat_fs->bs.fat_start + fat_fs->bs.fat_sectors;

	/*fat_length가 FAT 테이블의 인덱스 개수임에 주의하자.(그림에서 데이터 섹터가 4개니까 length=4)
	 얼핏 생각했을 땐 디스크 내에서 FAT 테이블이 차지하는 섹터의 개수로 오해할 수 있다.
	 디스크 내 FAT 테이블의 크기는 fat_sectors로 이미 fat_boot_create() 등에서 초기화해준다.
	 disk_size는 전체 디스크에 섹터가 몇개인가인데 거기서 sector_to_cluster하면 
	 */
	fat_fs->fat_length = sector_to_cluster(disk_size(filesys_disk)) -1 ;

}

/*----------------------------------------------------------------------------*/
/* FAT handling                                                               */
/*----------------------------------------------------------------------------*/

/* Add a cluster to the chain.
 * If CLST is 0, start a new chain.
 * Returns 0 if fails to allocate a new cluster. */
cluster_t fat_create_chain (cluster_t clst) {
	cluster_t new_clst = get_empty_cluster();  // fat bitmap의 처음부터 비어 있는 클러스터 찾기

	if (new_clst != 0){  // get_empty_cluster 리턴값이 0이면 실패.
		fat_put(new_clst, EOChain);
		if (clst != 0){  // 원래 클러스터가 0이면 새로 클러스터 체인을 만들어준다.
			fat_put(clst, new_clst);
		}
	}
	return new_clst;  // 실패하면 0 리턴
}

/* Remove the chain of clusters starting from CLST.
 * If PCLST is 0, assume CLST as the start of the chain. */
void
fat_remove_chain (cluster_t clst, cluster_t pclst) {
	/* TODO: Your code goes here. */
}

/* Update a value in the FAT table.
클러스터 넘버 clst 가 가리키는 FAT 엔트리를 val로 업데이트합니다. */
void
fat_put (cluster_t clst, cluster_t val) {
	/* TODO: Your code goes here. */
	ASSERT(clst>=1);
	if(!bitmap_test(fat_bitmap, clst-1)){
		bitmap_mark(fat_bitmap, clst-1);
	}
	fat_fs->fat[clst-1]=val;
}

/* Fetch a value in the FAT table. 
clst가 가리키는 클러스터 넘버를 리턴합니다.*/
cluster_t
fat_get (cluster_t clst) {
	/* TODO: Your code goes here. */
	ASSERT(clst>=1);
	if(clst>fat_fs->fat_length || !bitmap_test(fat_bitmap, clst-1)){
		return 0;
	}
	return fat_fs->fat[clst-1];
}

/* Covert a cluster # to a sector number.
클러스터 넘버 clst를 상응하는 섹터 넘버로 변환하고, 그 섹터 넘버를 리턴합니다. */
disk_sector_t
cluster_to_sector (cluster_t clst) {
	/* TODO: Your code goes here. */
	ASSERT(clst>=1);
	return fat_fs->data_start + (clst-1) * SECTORS_PER_CLUSTER;
}


// 섹터 번호에서 클러스터 번호를 구한다. 섹터 번호 8을 주면 클러스터 번호 4가 나와야함.(맨 위 그림 참고)
cluster_t sector_to_cluster (disk_sector_t sector){
	ASSERT(sector >= fat_fs->data_start);
	return sector - fat_fs->data_start+1; //(8 - 4 +1) =3 (0부터 세니까)
}

cluster_t get_empty_cluster(void){
	/* fat_bitmap에서 칸 하나가 false인 인덱스를 true로 바꿔주고 인덱스를 리턴한다.
	   비트맵 맨 처음부터 찾아나간다. */
	size_t clst = bitmap_scan_and_flip(fat_bitmap, 0, 1, false);
	
	if (clst == BITMAP_ERROR)
		return 0;  // 실패
	
	return (cluster_t) clst;
}