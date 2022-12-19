#ifndef FILESYS_FAT_H
#define FILESYS_FAT_H

#include "devices/disk.h"
#include "filesys/file.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t cluster_t;  /* Index of a cluster within FAT. */

#define FAT_MAGIC 0xEB3C9000 /* MAGIC string to identify FAT disk */
#define EOChain 0x0FFFFFFF   /* End of cluster chain */
#define FREE_ENTRY 0x00000000   /* fat_get(클러스터(=인덱스))를 했을 때 endofchain이 나오거나 어떤 숫자가 나오거나 하면 거긴 할당이 되어있는 곳인데 아무것도 나오지 않는다면?
                                   여긴 아직 할당이 안된 곳이구나를 알 수 있다. 즉 00000000의 FREE_ENTRY가 나온다면 할당할 수 있다는 소리임*/

/* Sectors of FAT information. */
#define SECTORS_PER_CLUSTER 1 /* Number of sectors per cluster */
#define FAT_BOOT_SECTOR 0     /* FAT boot sector. */
#define ROOT_DIR_CLUSTER 1    /* Cluster for the root directory */

void fat_init (void);
void fat_open (void);
void fat_close (void);
void fat_create (void);
void fat_close (void);

cluster_t fat_create_chain (
    cluster_t clst /* Cluster # to stretch, 0: Create a new chain */
);
void fat_remove_chain (
    cluster_t clst, /* Cluster # to be removed */
    cluster_t pclst /* Previous cluster of clst, 0: clst is the start of chain */
);
cluster_t fat_get (cluster_t clst);
void fat_put (cluster_t clst, cluster_t val);
disk_sector_t cluster_to_sector (cluster_t clst);
cluster_t sector_to_cluster (disk_sector_t sector);
cluster_t get_empty_cluster(void);
bool fat_alloc_get_multiple(size_t cnt, disk_sector_t *sectorp);

#endif /* filesys/fat.h */
