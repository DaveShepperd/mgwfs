#ifndef __MGWFS_H__
#define __MGWFS_H__

#define FSYS_FEATURES (FSYS_FEATURES_CMTIME|FSYS_FEATURES_JOURNAL)
#include "agcfsys.h"

//#define BITS_IN_BYTE 8
//#define MGWFS_MAGIC 0xFEEDF00D
//#define MGWFS_DEFAULT_BLOCKSIZE 4096
//#define MGWFS_DEFAULT_INODE_TABLE_SIZE 1024
//#define MGWFS_DEFAULT_DATA_BLOCK_TABLE_SIZE 1024
#define MGWFS_FILENAME_MAXLEN 255

/* Define filesystem structures */

extern struct mutex mgwfs_sb_lock;

typedef struct
{
	struct buffer_head *bh;	/* read data buffer */
	sector_t block;			/* block identifying the data in buffer */
	int buffOffset;
} MgwfsBlock_t;

typedef struct
{
	uint32_t inode_no;	/* offset into index.sys */
	uint32_t mode;		/* File access mode */
	void *contentsPtr;	/* pointer to file contents (used if type dir) */
	char *fileName;		/* pointer to filename once found */
	MgwfsBlock_t buffer;
	uint32_t size;		/* file size in bytes */
	uint32_t clusters;	/* number of clusters allocated for this file */
	uint32_t ctime;		/* file creation time */
	uint32_t mtime;		/* file modification time */
	FsysRetPtr pointers[FSYS_MAX_ALTS][FSYS_MAX_FHPTRS]; /* retrieval pointers */
} MgwfsInode_t;

#define MGWFS_MNT_OPT_ALLOCATION	1
#define MGWFS_MNT_OPT_COPIES		2
#define MGWFS_MNT_OPT_VERBOSE		4
#define MGWFS_MNT_OPT_VERBOSE_HOME	8
#define MGWFS_MNT_OPT_VERBOSE_HEADERS 16
#define MGWFS_MNT_OPT_VERBOSE_DIR	32
#define MGWFS_MNT_OPT_VERBOSE_READ	64
#define MGWFS_MNT_OPT_VERBOSE_INODE	128
#define MGWFS_MNT_OPT_VERBOSE_INDEX	256
#define MGWFS_MNT_OPT_VERBOSE_FREE	512
#define MGWFS_MNT_OPT_ANY_VERBOSE (4|8|16|32|64|128|256)

typedef struct MgwfsSuper_t
{
	struct super_block *hisSb; /* Just for reference */
	FsysHomeBlock homeBlk;	/* A complete copy of our home block from disk */
	uint32_t baseSector;	/* sector offset to start of our fs if in a partition */
	FsysHeader indexSysHdr;	/* copy of the file header of index.sys */
	uint32_t *indexSys;		/* contents of index.sys */
	int indexFHDirty;		/* flag indicating index file header is dirty */
	int indexSysDirty;		/* flag indicating index.sys contents is dirty */
	FsysHeader freeMapHdr;	/* copy of file header of freemap.sys */
	int freeMapEntries;		/* Number of entries in freemap */
	FsysRetPtr *freeMap;	/* contents of freemap.sys */
	int freeListFHDirty;	/* flag indicating freeMapHdr is dirty */
	int freeListDirty;		/* flag indicating freelist contents is dirty */
	int defaultAllocation;	/* Default number of sectors to allocate on file extend */
	int defaultCopies;		/* Default number of copies to make of new files */
	MgwfsBlock_t buffer;
	uint32_t flags;			/* for now, just verbose flags (see MGWFS_MNT_OPT_VERBOSE_xxx flags above) */
} MgwfsSuper_t;

extern uint8_t* mgwfs_getSector(struct super_block *sb, MgwfsBlock_t *buffp, sector_t sector, int *numBytes);
extern void mgwfs_putSector(struct super_block *sb, const uint8_t *ptr, sector_t sector);
extern void mgwfs_putSectors(struct super_block *sb, const uint8_t *ptr, sector_t stSector, int numSectors);
extern int mgwfs_getFileHeader(struct super_block *sb, const char *title, uint32_t fhID, uint32_t fileID, uint32_t lbas[FSYS_MAX_ALTS], FsysHeader *fhp);
extern int mgwfs_allocFileHeader(struct super_block *sb, uint32_t *fid, FsysHeader *fhp);
extern int mgwfs_readFile(struct super_block *sb, const char *title, uint8_t *dst, int bytes, FsysRetPtr *retPtr, int squawk);

typedef struct
{
	FsysRetPtr result;		/* newly formed selection */
	FsysRetPtr *hints;		/* hint of what to connect to if possible */
	uint32_t minSector;		/* minimum sector to look for */
	int currListAlloc;		/* maximum number of entries in list */
//	int updatedEntryIndex;	/* index of entry updated */
//	int addedEntryIndex;	/* index of entry added */
	int allocChange;		/* number of entries added or deleted */
} MgwfsFoundFreeMap_t;

extern void mgwfsDumpFreeMap( const char *title, const FsysRetPtr *list );
extern int mgwfsFindFree(MgwfsSuper_t *ourSuper, MgwfsFoundFreeMap_t *stuff, int numSectors );
extern int mgwfsFreeSectors(MgwfsSuper_t *ourSuper, MgwfsFoundFreeMap_t *stuff, FsysRetPtr *retp);

#if 0
#define MGWFS_SB_FLAG_VERBOSE (1)
struct mgwfs_superblock
{
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
	struct mgwfs_superblock *mgwfs_sb)
{
	return mgwfs_sb->blocksize / sizeof(struct mgwfs_inode);
}

static inline uint64_t MGWFS_DATA_BLOCK_TABLE_START_BLOCK_NO_HSB(
	struct mgwfs_superblock *mgwfs_sb)
{
	return MGWFS_INODE_TABLE_START_BLOCK_NO
		   + mgwfs_sb->inode_table_size / MGWFS_INODES_PER_BLOCK_HSB(mgwfs_sb)
		   + 1;
}
#endif

#endif /*__MGWFS_H__*/
