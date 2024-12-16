#ifndef __MGWFS_H__
#define __MGWFS_H__

#define BITS_IN_BYTE 8
#define MGWFS_MAGIC 0x20160105
#define MGWFS_DEFAULT_BLOCKSIZE 4096
#define MGWFS_DEFAULT_INODE_TABLE_SIZE 1024
#define MGWFS_DEFAULT_DATA_BLOCK_TABLE_SIZE 1024
#define MGWFS_FILENAME_MAXLEN 255

/* Define filesystem structures */

extern struct mutex mgwfs_sb_lock;

struct mgwfs_dir_record {
    char filename[MGWFS_FILENAME_MAXLEN];
    uint64_t inode_no;
};

struct mgwfs_inode {
    mode_t mode;
    uint64_t inode_no;
    uint64_t data_block_no;

    // TODO struct timespec is defined kenrel space,
    // but mkfs-mgwfs.c is compiled in user space
    /*struct timespec atime;
    struct timespec mtime;
    struct timespec ctime;*/

    union {
        uint64_t file_size;
        uint64_t dir_children_count;
    };
};

#define MGWFS_SB_FLAG_VERBOSE (1)

struct mgwfs_superblock {
    uint64_t version;
    uint64_t magic;
    uint64_t blocksize;

    uint64_t inode_table_size;
    uint64_t inode_count;

    uint64_t data_block_table_size;
    uint64_t data_block_count;
	
	uint32_t flags;
	uint32_t misc;
	
	uint64_t fs_size;
};

static const uint64_t MGWFS_SUPERBLOCK_BLOCK_NO = 0;
static const uint64_t MGWFS_INODE_BITMAP_BLOCK_NO = 1;
static const uint64_t MGWFS_DATA_BLOCK_BITMAP_BLOCK_NO = 2;
static const uint64_t MGWFS_INODE_TABLE_START_BLOCK_NO = 3;

static const uint64_t MGWFS_ROOTDIR_INODE_NO = 0;
// data block no is the absolute block number from start of device
// data block no offset is the relative block offset from start of data block table
static const uint64_t MGWFS_ROOTDIR_DATA_BLOCK_NO_OFFSET = 0;

/* Helper functions */

static inline uint64_t MGWFS_INODES_PER_BLOCK_HSB(
        struct mgwfs_superblock *mgwfs_sb) {
    return mgwfs_sb->blocksize / sizeof(struct mgwfs_inode);
}

static inline uint64_t MGWFS_DATA_BLOCK_TABLE_START_BLOCK_NO_HSB(
        struct mgwfs_superblock *mgwfs_sb) {
    return MGWFS_INODE_TABLE_START_BLOCK_NO
           + mgwfs_sb->inode_table_size / MGWFS_INODES_PER_BLOCK_HSB(mgwfs_sb)
           + 1;
}

#endif /*__MGWFS_H__*/