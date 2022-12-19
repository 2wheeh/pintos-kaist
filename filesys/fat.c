#include "filesys/fat.h"
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "bitmap.h"

#include "filesys/directory.h"

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
	unsigned int fat_sectors; /* Size of FAT in sectors. fat가 차지하는 섹터 개수 */
	unsigned int root_dir_cluster;
};

/* FAT FS 
FAT파일 시스템 정보를 담고 있는 구조체*/
struct fat_fs {
	struct fat_boot bs; //부트섹터의 내용을 저장하는 구조체. 파일 시스템에 대한 정보를 셋팅
	unsigned int *fat;  //? PintOS가 부팅되면서 FAT를 Disk에서 읽어오고 메모리에 올리는데, 이때 FAT가 올라간 메모리의 시작 주소
	unsigned int fat_length; /* FAT의 entry 갯수 (FAT에서 관리하는 cluster의 갯수 = Data area의 sector 수) */
	disk_sector_t data_start;  /* Filesys Disk에서 FAT area이후, Data area가 시작되는 첫번째 sector */
	cluster_t last_clst;  /* 추측: FAT의 마지막 클러스터 번호 = data_start - 1 이 될것으로 에상함. (그런데 이걸 굳이 따로 멤버로 저장해두어야 할까?) */
	struct lock write_lock;
};

static struct fat_fs *fat_fs;
static struct cluster_map *cluster_map;

void fat_boot_create (void);
void fat_fs_init (void);

bool fat_alloc_get_multiple(size_t cnt, disk_sector_t *sectorp);
bool fat_alloc_get_clst(disk_sector_t *sectorp);

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
	// printf("!!!!!!!!!!!!!!%d\n", disk_size(filesys_disk)); //4032
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
	printf("filesys_disk %d\n", filesys_disk);

	printf("@@(disk_size (filesys_disk) - 1) %d\n", (disk_size (filesys_disk) - 1));
	printf("##(sizeof (cluster_t) * SECTORS_PER_CLUSTER %d\n", sizeof (cluster_t) * SECTORS_PER_CLUSTER);

	printf("!!!!!!!!!!!!!!fat_total_sectors%d\n", fat_fs->bs.total_sectors); //4032
	printf("!!!!!!!!!!!!!!fat_sectors%d\n", fat_fs->bs.fat_sectors); //32
}


void
fat_fs_init (void) {
	/* TODO: Your code goes here. */
	 
	// 파일시스템에 몇 개의 클러스터가 있는지에 대한 정보
	/* 클러스터는 Data area에서의 논리적 단위이므로, 
	[ 파일시스템에 존재하는 클러스터의 갯수 = ( filesys disk의 전체 섹터 수 - ( boot sector(1개) + fat가 차지하는 섹터 수 ) ) / 클러스터당 섹터 수 ] 이다. */
	fat_fs->fat_length = (fat_fs->bs.total_sectors - (1 + fat_fs->bs.fat_sectors)) / (fat_fs->bs.sectors_per_cluster);
	printf("!!!fat_length%d\n", fat_fs->fat_length);
	// 어떤 섹터에서 파일 저장을 시작할 수 있는지에 대한 정보
    /* [ boot sector(1개) + fat가 차지하는 섹터 수 ] 이후 부터 데이터 영역으로 사용 가능한 섹터가 존재한다. */
	fat_fs->data_start = 1 + fat_fs->bs.fat_sectors;
	fat_fs->last_clst = fat_fs->fat_length -1;

	lock_init(&fat_fs->write_lock); //? 이 락은 왜 거는걸까? 어느부분의 원자성을 보장하려고?
}

/*----------------------------------------------------------------------------*/
/* FAT handling                                                               */
/*----------------------------------------------------------------------------*/

/* Add a cluster to the chain.
 * If CLST is 0, start a new chain.
 * Returns 0 if fails to allocate a new cluster. 
* clst 인자(클러스터 인덱싱 넘버)로 특정된 클러스터 뒤에 다른 클러스터를 추가함으로써 체인을 연장합니다.
* 만약 clst가 0이라면, 새로운 체인을 만듭니다.
* 새롭게 할당된 클러스터의 넘버를 리턴합니다. */
cluster_t fat_create_chain (cluster_t clst) {

	ASSERT(clst < fat_fs->fat_length);
	cluster_t new_clst = 0;
	fat_alloc_get_clst(&new_clst); 

	ASSERT(new_clst != EOChain);

	if(clst !=0){
		fat_put(clst, new_clst);
	}
	return new_clst;

	// cluster_t new_clst = get_empty_cluster();  // fat bitmap의 처음부터 비어 있는 클러스터 찾기

	// if (new_clst != 0){  // get_empty_cluster 리턴값이 0이면 실패.
	// 	fat_put(new_clst, EOChain);
	// 	if (clst != 0){  // 원래 클러스터가 0이면 새로 클러스터 체인을 만들어준다.
	// 		fat_put(clst, new_clst);
	// 	}
	// }
	// return new_clst;  // 실패하면 0 리턴
}

/* Remove the chain of clusters starting from CLST.
 * If PCLST is 0, assume CLST as the start of the chain.
  * clst로부터 시작하여, 체인으로부터 클러스터를 제거합니다. 
  * pclst는 체인에서의 clst 직전 클러스터여야 합니다. 
  * 이 말은, 이 함수가 실행되고 나면 pclst가 업데이트된 체인의 마지막 원소가 될 거라는 말입니다. 
  * 만일 clst가 체인의 첫 번째 원소라면, pclst의 값은 0이어야 할 겁니다. */
void
fat_remove_chain (cluster_t clst, cluster_t pclst) {
	/* TODO: Your code goes here. */

	// data start보다 큰 값의 clst를 잠정적으로 sector번호라고 판단.
 	if(fat_fs->data_start <= clst) {
         clst = sector_to_cluster(clst);
     }
	
	cluster_t cur, next;
	if(pclst != 0){
		ASSERT(fat_get(pclst) == clst);
	}

	cur = clst;
	do{
		next = fat_get(cur);
		fat_put(cur, FREE_ENTRY); //? 이건 정의가 안되어있네??
		cur =next;
		ASSERT(cur != FREE_ENTRY);
	}while(cur != EOChain);
}
void
fat_alloc_free(cluster_t clst){
	fat_remove_chain(clst,0);
}
/* Update a value in the FAT table.
* 클러스터 넘버 clst 가 가리키는 FAT 엔트리를 val로 업데이트합니다. 
  * FAT에 있는 각 엔트리는 체인에서의 다음 클러스터를 가리키고 있기 때문에 
  * (만약 존재한다면 그렇다는 거고, 다음 클러스터가 존재하지 않으면 EOChain (End Of Chain)입니다), 
  * 이 함수는 연결관계를 업데이트하기 위해 사용될 수 있습니다. */
void
fat_put (cluster_t clst, cluster_t val) {
	/* TODO: Your code goes here. */

	*(fat_fs->fat + clst) = val;


	// ASSERT(clst>=1);
	// if(!bitmap_test(fat_bitmap, clst-1)){
	// 	bitmap_mark(fat_bitmap, clst-1);
	// }
	// fat_fs->fat[clst-1]=val;
}

/* Fetch a value in the FAT table. 
clst가 가리키는 클러스터 넘버를 리턴합니다.*/
cluster_t
fat_get (cluster_t clst) {
	/* TODO: Your code goes here. */

	return (cluster_t)*(fat_fs->fat + clst);
	// ASSERT(clst>=1);
	// if(clst>fat_fs->fat_length || !bitmap_test(fat_bitmap, clst-1)){
	// 	return 0;
	// }
	// return fat_fs->fat[clst-1];
}

/* Covert a cluster # to a sector number.
클러스터 넘버 clst를 상응하는 섹터 넘버로 변환하고, 그 섹터 넘버를 리턴합니다. */
disk_sector_t
cluster_to_sector (cluster_t clst) {
	/* TODO: Your code goes here. */
	return fat_fs->data_start + clst;
	// ASSERT(clst>=1);
	// return fat_fs->data_start + (clst-1) * SECTORS_PER_CLUSTER;
}

/* 섹터 넘버 sect를 상응하는 클러스터 넘버로 변환하고, 그 클러스터 넘버를 리턴합니다. */
cluster_t sector_to_cluster(disk_sector_t sect) {
	return sect - fat_fs->data_start;
}


bool fat_alloc_get_multiple(size_t cnt, disk_sector_t *sectorp) {
     ASSERT(0 < cnt && cnt <= fat_fs->last_clst);
	if(cnt ==0) return true;

     int clst_number = 2;
     cluster_t prev = EOChain;

     while(cnt > 0 && clst_number <= fat_fs->last_clst) {
         if(fat_get(clst_number) == FREE_ENTRY) {
             fat_put(clst_number, prev);
             prev = clst_number;
             cnt--;
         }
         clst_number++;
     }

     if(cnt != 0) {
         PANIC("filesys disk는 줄게 읎어....");
     }

     if(prev != EOChain) {
         *sectorp = cluster_to_sector(prev);
     }
     return prev != EOChain;
 }

 bool
 fat_alloc_get_clst(cluster_t *sectorp) {
     return fat_alloc_get_multiple(1, sectorp);
 }

/* START로 부터 COUNT만큼 떨어진 클러스터에 대응하는 섹터 번호를 반환한다. */
cluster_t
 fat_find_count(disk_sector_t start, size_t count) {
     ASSERT(count >= 0);
     cluster_t cursor = sector_to_cluster(start);

     while(count > 0) {
         cursor = fat_get(cursor);
         count--;
     }

     ASSERT(cursor != EOChain);

     if(count > 0) {
         PANIC("count > cluster length");
     }

     return cluster_to_sector(cursor);
 }


// // 섹터 번호에서 클러스터 번호를 구한다. 섹터 번호 8을 주면 클러스터 번호 4가 나와야함.(맨 위 그림 참고)
// cluster_t sector_to_cluster (disk_sector_t sector){
// 	// ASSERT(sector >= fat_fs->data_start);
// 	return sector - fat_fs->data_start+1; //(8 - 4 +1) =3 (0부터 세니까)
// }

// cluster_t get_empty_cluster(void){
// 	/* fat_bitmap에서 칸 하나가 false인 인덱스를 true로 바꿔주고 인덱스를 리턴한다.
// 	   비트맵 맨 처음부터 찾아나간다. */
// 	size_t clst = bitmap_scan_and_flip(fat_bitmap, 0, 1, false);
	
// 	if (clst == BITMAP_ERROR)
// 		return 0;  // 실패
	
// 	return (cluster_t) clst;
// }