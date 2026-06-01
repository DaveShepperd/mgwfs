#ifndef __MGWFS_H__
#define __MGWFS_H__

#define _LARGEFILE64_SOURCE 
#define FUSE_USE_VERSION 31
#define NO_MUTEXES 1
#ifndef TRUE
#define TRUE (1)
#endif
#ifndef FALSE
#define FALSE (0)
#endif

#include <fuse3/fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <linux/magic.h>
#include <sys/vfs.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <ctype.h>
#if !NO_MUTEXES
#include <pthread.h>
#endif

typedef uint32_t sector_t;
#define FSYS_FEATURES (FSYS_FEATURES_CMTIME|FSYS_FEATURES_JOURNAL)
#include "agcfsys.h"

#define MGWFS_FILENAME_MAXLEN 255		/* Techincally, the spec says filenames could be 256 bytes long, but we limit them here */
#define MGWFS_MAX_NEST_LEVEL (64)		/* Arbitary limit on how deeply nested directories may go (just for sanity's sake) */

#define n_elts(x) (int)(sizeof(x)/sizeof(x[0]))

typedef struct
{
	uint8_t *buff;
	uint32_t buffSize;	/* Size of read/write buffer (will be multiple of sector size) */
	off_t buffOffset;		/* Offset of last byte read from or written to rwBuff */
	uint32_t buffUsed;	/* Total bytes used in rdBuff */
	int buffErr;
} RwBuff_t;

typedef struct
{
	int index;				/* Our index into the list */
	uint32_t inode;			/* file ID of open file */
	int instances;			/* number of times this file is open() */
	int openFlags;			/* flags passed in on open() */
} FuseFH_t;

typedef struct
{
	uint32_t lba[FSYS_MAX_ALTS];
} IndexSys_t;

typedef struct MgwfsInode_t
{
	int idxParentInode;				/* Index to parent directory's inode (i.e. super->inodeList[xx]) */
	int idxNextInode;				/* Index to next inode in this directory (i.e. super->inodeList[xx]) */
	int idxPrevInode;				/* Index to previous inode in this directory (i.e. super->inodeList[xx]) */
	int idxChildTop;				/* Index to list of inodes if this is a directory */
	int numInodes;					/* number of inodes in this directory */
	uint32_t inode_no;				/* file's local ID (relative to indexSys) */
	IndexSys_t fhSectors;			/* on disk sector ID's to copies of FH (from index.sys) */
	mode_t mode;					/* file's mode */
	int fnLen;						/* Filename length */
	FsysHeader fsHeader;			/* File's header */
	RwBuff_t rwb;
	uint32_t flags;					/* MGWFS_INODE_* bits (see below) */
	char fileName[MGWFS_FILENAME_MAXLEN+1];	/* File's name */
} MgwfsInode_t;

/* Bits for MgwfsInode_t.flags */
#define MGWFS_INODE_BOOT_IDX	(0)		/* file's boot index (2 bits: value 0 to 3) */
#define MGWFS_INODE_BOOT_MASK	(3)		/* file's boot index (2 bits: value 0 to 3) */
#define MGWFS_INODE_ANY_BOOT	(1<<2)	/* file is set as a boot file (which file of 4 in bits 0&1)*/
#define MGWFS_INODE_JOURNAL		(1<<3)	/* file is set as journal */
#define MGWFS_INODE_MTIME_SET	(1<<4)	/* mtime was set explicitly (e.g. via utimens); do not restamp on flush */

enum
{
	VERB_BIT_MINIMUM,
	VERB_BIT_HOME,
	VERB_BIT_HEADERS,
	VERB_BIT_RETPTRS,
	VERB_BIT_READ,
	VERB_BIT_INDEX,
	VERB_BIT_FREE,
	VERB_BIT_FREEMAP,
	VERB_BIT_VERIFY_FREEMAP,
	VERB_BIT_DMPROOT,
	VERB_BIT_UNPACK,
	VERB_BIT_LOOKUP,
	VERB_BIT_LOOKUP_ALL,
	VERB_BIT_ITERATE,
	VERB_BIT_FUSE,
	VERB_BIT_FUSE_CMD,
	VERB_BIT_WRITES,
#if !NO_MUTEXES
	VERB_BIT_LOCKS,
#endif
	VERB_BIT_MAX
};

#define VERBOSE_MINIMUM		(1<<VERB_BIT_MINIMUM)	/* display the minimum */
#define VERBOSE_HOME		(1<<VERB_BIT_HOME)		/* display home block */
#define VERBOSE_HEADERS		(1<<VERB_BIT_HEADERS)	/* display file headers */
#define VERBOSE_RETPTRS		(1<<VERB_BIT_RETPTRS)	/* display retrieval pointers in file headers */
#define VERBOSE_READ		(1<<VERB_BIT_READ)		/* display read requests */
#define VERBOSE_INDEX		(1<<VERB_BIT_INDEX)		/* display index.sys file and header */
#define VERBOSE_FREE		(1<<VERB_BIT_FREE)		/* display free primitives */
#define VERBOSE_FREEMAP		(1<<VERB_BIT_FREEMAP)	/* display freemap file and header */
#define VERBOSE_VERIFY_FREEMAP	(1<<VERB_BIT_VERIFY_FREEMAP)	/* display freemap file and header */
#define VERBOSE_DMPROOT		(1<<VERB_BIT_DMPROOT)	/* dump root directory contents and header */
#define VERBOSE_UNPACK		(1<<VERB_BIT_UNPACK)	/* Display details during unpack() */
#define VERBOSE_LOOKUP		(1<<VERB_BIT_LOOKUP)	/* Show instances of directory searches */
#define VERBOSE_LOOKUP_ALL	(1<<VERB_BIT_LOOKUP_ALL)/* Show details doing directory searches */
#define VERBOSE_ITERATE		(1<<VERB_BIT_ITERATE)	/* iterate directory tree */
#define VERBOSE_FUSE		(1<<VERB_BIT_FUSE)		/* Show fuse stuff */
#define VERBOSE_FUSE_CMD	(1<<VERB_BIT_FUSE_CMD)	/* Show fuse commands */
#define VERBOSE_WRITES		(1<<VERB_BIT_WRITES)	/* Show details of anything related to file writes */
#if !NO_MUTEXES
#define VERBOSE_LOCKS		(1<<VERB_BIT_LOCKS)		/* Show details of lock/unlock */
#endif
#define VERBOSE_ANY			((1<<VERB_BIT_MAX)-1)	/* Any verbose bit */

//#define MAX_DIRTY_INODE 100

typedef struct
{
	RwBuff_t rwBuff;
	uint32_t sectorsFree;	/* Total number of free sectors */
	uint32_t sectorsUsed;	/* Total number of used sectors */
	uint32_t sectorsLost;	/* Total number of sectors lost track of */
	int freeMapEntriesUsed;	/* Number of entries used in freemap */
	int freeMapEntriesAvail;/* Maximum number of freemap entries available */
} FreeMap_t;

#define FREEMAP_RP_PTR(ptr) (FsysRetPtr *)(ptr->rwBuff.buff)

#define SPECIAL_DIRTY_INDEX 0x01	/* index.sys is dirty */
#define SPECIAL_DIRTY_FREE	0x02	/* freemap.sys is dirty */
#define SPECIAL_DIRTY_HOME	0x04	/* homeblock is dirty */

#define MAX_NUM_BOOT_FILES (4)

typedef struct MgwfsSuper_t
{
	/* Not sure if a mutex is needed, but just to be safe we use one to force single threading */
	int fd;					/* file descriptor used to read/write image file */
	const char *imageName;	/* path to the image file */
	uint32_t verbose;		/* verbose flags */
	int defaultAllocation;	/* Default number of sectors to allocate on file extend */
	int defaultCopies;		/* Default number of copies to make of new files */
	uint32_t baseSector;	/* sector offset to start of our fs if in a partition */
	uint32_t maxHb;			/* maximum home block sector */
	uint32_t homeLbas[FSYS_MAX_ALTS];	/* sectors where to find home blocks */
	uint32_t bootIndicies[MAX_NUM_BOOT_FILES]; /* FID's of boot files (0=none) */
	FsysHomeBlock homeBlk;	/* A copy of our home block from disk */
	FsysHeader indexSysHdr;	/* copy of the file header of index.sys */
	IndexSys_t *indexSys;	/* Contents of index.sys file */
	MgwfsInode_t **inodeList;
	int numInodesUsed;		/* number of items in list */
	int numInodesAvailable; /* number of items available in list */
	FreeMap_t freeMap;		/* Contents of freemap.sys file */
	int *dirtyInodes;		/* List of inodes to write back to disk */
	int numDirtyInodes;		/* Number of items in dirtyInodes */
	int numDirtyInodesAvailable; /* Number of items in dirtyInodes */
	int specialDirtys;
	FILE *logFile;			/* Defaults to stdout */
	FILE *errFile;			/* Defaults to stderr */
	FuseFH_t *fuseFHs;		/* list of fuse open files */
	int numFuseFHs;			/* number of items available in fuseFHs */
} MgwfsSuper_t;

#include "mgwfsctl.h"

#if !NO_MUTEXES
extern void mgwfs_destroy_mutex(void);
extern void fuse_destroy_mutex(void);
extern void mgwfs_lock_it(const char *name, MgwfsSuper_t *ourSuper, pthread_mutex_t *mutex, const char *fileName, int lineNo);
extern void mgwfs_unlock_it(const char *name, MgwfsSuper_t *ourSuper, pthread_mutex_t *mutex, const char *fileName, int lineNo);
#define DEBUG_LOCKS (1)
#if DEBUG_LOCKS
#define LOCK_IT(name, ss, xx) mgwfs_lock_it(name, ss, xx, __FILE__, __LINE__ )
#define UNLOCK_IT(name, ss, xx) mgwfs_unlock_it(name, ss, xx, __FILE__, __LINE__)
#else
#define LOCK_IT(name, ss, xx) pthread_mutex_lock(xx)
#define UNLOCK_IT(name, ss, xx) pthread_mutex_unlock(xx)
#endif
#else
#define LOCK_IT(name, ss, xx) do { ; } while (0)
#define UNLOCK_IT(name, ss, xx) do { ; } while (0)
#endif

typedef struct
{
	FsysRetPtr result;		/* newly formed selection */
	FsysRetPtr actual;		/* RP of actual returned sectors */
	FsysRetPtr hint;		/* hint of what to connect to if possible */
	uint32_t minSector;		/* minimum sector to look for */
} MgwfsFoundFreeMap_t;

#ifndef S_IFREG
#define S_IFREG 0100000
#endif
#ifndef S_IFDIR
#define S_IFDIR 0040000
#endif

typedef struct part
{
    uint8_t status;
    uint8_t st_head;
    uint8_t st_sectcyl[2];
    uint8_t type;
    uint8_t en_head;
    uint8_t en_sectcyl[2];
    uint8_t abs_sect[4];
    uint8_t num_sects[4];
} Partition;

typedef struct
{
    uint8_t jmp[3];          /* 0x000 x86 jump */
    uint8_t oem_name[8];     /* 0x003 OEM name */
    uint8_t bps[2];          /* 0x00B bytes per sector */
    uint8_t sects_clust;     /* 0x00D sectors per cluster */
    uint16_t num_resrv;      /* 0x00E number of reserved sectors */
    uint8_t num_fats;        /* 0x010 number of FATs */
    uint8_t num_roots[2];    /* 0x011 number of root directory entries */
    uint8_t total_sects[2];  /* 0x013 total sectors in volume */
    uint8_t media_desc;      /* 0x015 media descriptor */
    uint16_t sects_fat;      /* 0x016 sectors per FAT */
    uint16_t sects_trk;      /* 0x018 sectors per track */
    uint16_t num_heads;      /* 0x01A number of heads */
    uint32_t num_hidden;     /* 0x01C number of hidden sectors */
    uint32_t total_sects_vol;/* 0x020 total sectors in volume */
    uint8_t drive_num;       /* 0x024 drive number */
    uint8_t reserved0;       /* 0x025 unused */
    uint8_t boot_sig;        /* 0x026 extended boot signature */
    uint8_t vol_id[4];       /* 0x027 volume ID */
    uint8_t vol_label[11];   /* 0x02B volume label */
    uint8_t reserved1[8];    /* 0x036 unused */
    uint8_t bootstrap[384];  /* 0x03E boot code */
    Partition parts[4];      /* 0x1BE partition table */
    uint16_t end_sig;        /* 0x1FE end signature */
} BootSector_t; // __attribute__ ((packed));

extern BootSector_t bootSect;
extern MgwfsSuper_t ourSuper;

/* Funcions in mgwfs.c */
/* File primitives called by fuse functions */
extern int fileOpen(const char *title, const char *path, MgwfsSuper_t *ourSuper, FuseFH_t *fhp);
extern int fileClose(const char *title, MgwfsSuper_t *ourSuper, FuseFH_t *fhp);
extern int fileRename(const char *title, MgwfsSuper_t *ourSuper, const char *oldPath, const char *newPath);
//extern int fileRead(const char *title, MgwfsSuper_t *ourSuper, FuseFH_t *fhp, off_t offset, size_t bytes);
extern int fileCreate(const char *title, const char *path, MgwfsSuper_t *ourSuper);
extern int fileExtend(const char *title, MgwfsSuper_t *ourSuper, FuseFH_t *fhp);
extern int fileWrite(const char *title, MgwfsSuper_t *ourSuper, FuseFH_t *fhp, off_t offset, size_t bytes);
extern int fileFlush(const char *title, MgwfsSuper_t *ourSuper, FuseFH_t *fhp);

extern void displayFileHeader(FILE *outp, FsysHeader *fhp, int retrievalsToo);
extern void displayHomeBlock(FILE *outp, const FsysHomeBlock *homeBlkp, uint32_t cksum);
extern int getHomeBlock(MgwfsSuper_t *ourSuper, off64_t maxHb, off64_t sizeInSectors, uint32_t *ckSumP);
extern int getFileHeader(const char *title, MgwfsSuper_t *ourSuper, uint32_t id, IndexSys_t *lbas, FsysHeader *fhp);
extern int readWholeFile(const char *title,  MgwfsSuper_t *ourSuper, uint8_t *dst, int bytes, FsysRetPtr *retPtr);
//extern int writeWholeFile(const char *title,  MgwfsSuper_t *ourSuper, FuseFH_t *fhp);
extern int flushFile(const char *title, MgwfsSuper_t *ourSuper, FuseFH_t *fhp);
extern void dumpIndex(FILE *outp, IndexSys_t *indexBase, int bytes);
extern int dumpFreemap(FILE *outp, const char *title, FsysRetPtr *rpBase, int maxEntries, uint32_t *totSectors );
extern void dumpDir(FILE *outp, uint8_t *dirBase, int bytes, MgwfsSuper_t *ourSuper, IndexSys_t *indexSys );
extern void verifyFreemap(MgwfsSuper_t *ourSuper);
extern int unpackDir(MgwfsSuper_t *ourSuper, MgwfsInode_t *inode, int nest);
extern int tree(MgwfsSuper_t *ourSuper, int topIdx, int nest);
extern int findInode(MgwfsSuper_t *ourSuper, int topIdx, const char *path);
extern int countSectors(FsysRetPtr *rp, int maxRps, uint32_t *totalSectors);
extern FuseFH_t *getFuseFHidx(MgwfsSuper_t *ourSuper, uint64_t idx);
extern void freeFuseFHidx(MgwfsSuper_t *ourSuper, uint64_t idx);
//extern int writeHomeBlock(MgwfsSuper_t *super);
//extern int writeIndexSys(MgwfsSuper_t *super);
//extern int writeFreeMapSys(MgwfsSuper_t *super);
extern int writeFileHeader(MgwfsSuper_t *super, MgwfsInode_t *inode);
extern int writeDirectory(MgwfsSuper_t *super, MgwfsInode_t *dir);
extern MgwfsInode_t *findUnusedInode(MgwfsSuper_t *super);
extern int updateAllMetaData(const char *title, MgwfsSuper_t *ourSuper);
extern void addToDirty(const char *title, MgwfsSuper_t *super, int idx);

/* functions in freemap.c */
#define FREEM_FLAG_MARK_DIRTY	(0x01)
//#define FREEM_FLAG_TRY_ONLY		(0x02)
//#define FREEM_FLAG_NO_RESCAN	(0x04)
extern void mgwfsDumpFreeMap( MgwfsSuper_t *ourSuper, const char *title, const FsysRetPtr *list );
extern int mgwfsFindFree(MgwfsSuper_t *ourSuper, MgwfsFoundFreeMap_t *stuff, int numSectors, uint32_t flags );
extern int mgwfsFreeSectors(MgwfsSuper_t *ourSuper, FsysRetPtr *retp, uint32_t flags);
/*
 * Command line options
 */
typedef struct
{
	unsigned long filler;		/* Fuse like to clobber this with a 0xffffff for some reason */
	unsigned long allocation;
	unsigned long copies;
	unsigned long verbose;
	unsigned long show_help;
	unsigned long quit;
	unsigned long show_version;
	unsigned long read_write;
	const char *image;
	const char *logFile;
	const char *testPath;
} Options_t;

extern Options_t options;

/* Funcions in fuse.c */
extern const struct fuse_operations mgwfs_oper;

#endif /*__MGWFS_H__*/
