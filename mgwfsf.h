#ifndef __MGWFSF_H__
#define __MGWFSF_H__

#define _LARGEFILE64_SOURCE 
#define FUSE_USE_VERSION 31

#include <fuse.h>
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
#include <pthread.h>

typedef uint32_t sector_t;
#define FSYS_FEATURES (FSYS_FEATURES_CMTIME|FSYS_FEATURES_JOURNAL)
#include "agcfsys.h"

#define MGWFS_FILENAME_MAXLEN 255

#define n_elts(x) (sizeof(x)/sizeof(x[0]))

typedef struct MgwfsInode_t
{
	int idxParentInode;				/* Index to parent directory's inode */
	int idxNextInode;				/* Index to next inode in this directory */
	int idxChildTop;				/* Index to list of inodes if this is a directory */
	int numInodes;					/* number of inodes in this directory */
	uint32_t inode_no;				/* file's local ID (relative to indexSys) */
	mode_t mode;					/* file's mode */
	int fnLen;						/* Filename length */
	FsysHeader fsHeader;			/* File's header */
	char fileName[MGWFS_FILENAME_MAXLEN+1];	/* File's name */
} MgwfsInode_t;

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
#define VERBOSE_ANY			((1<<VERB_BIT_MAX)-1)	/* Any verbose bit */

typedef struct
{
	int index;				/* Our index into the list */
	uint32_t inode;			/* file ID of open file */
	int instances;			/* number of times this file is open() */
	uint8_t *buffer;		/* file content buffer */
	int bufferSize;			/* size of content buffer */
	uint32_t offset;		/* number of bytes read from this file so far */
	int readAmt;			/* return value from readFile() */
} FuseFH_t;

typedef struct MgwfsSuper_t
{
	int fd;					/* fd of image file */
	const char *imageName;	/* path to our image */
	uint32_t verbose;		/* verbose flags */
	int defaultAllocation;	/* Default number of sectors to allocate on file extend */
	int defaultCopies;		/* Default number of copies to make of new files */
	MgwfsInode_t *inodeList; /* List of our files maintained as local inodes */
	int numInodesUsed;		/* number of items in list */
	int numInodesAvailable; /* number of items available in list */
	FsysHomeBlock homeBlk;	/* A copy of our home block from disk */
	FsysHeader indexSysHdr;	/* copy of the file header of index.sys */
	uint32_t baseSector;	/* sector offset to start of our fs if in a partition */
	uint32_t *indexSys;		/* contents of index.sys */
	int indexSysDirty;		/* flag indicating index.sys contents is dirty */
	int freeMapEntriesUsed;	/* Number of entries used in freemap */
	int freeMapEntriesAvail;/* Maximum number of freemap entries available */
	FsysRetPtr *freeMap;	/* contents of freemap.sys */
	int freeListDirty;		/* flag indicating freelist contents is dirty */
	FILE *logFile;			/* Defaults to stdout */
	FuseFH_t *fuseFHs;		/* list of fuse open files */
	int numFuseFHs;			/* number of items available in fuseFHs */
} MgwfsSuper_t;

typedef struct
{
	FsysRetPtr result;		/* newly formed selection */
	FsysRetPtr *hints;		/* hint of what to connect to if possible */
	uint32_t minSector;		/* minimum sector to look for */
	int listUsed;			/* number of entries in list used */
	int listAvailable;		/* number of entries in list available */
	int allocChange;		/* number of entries added or deleted */
} MgwfsFoundFreeMap_t;

extern void mgwfsDumpFreeMap( MgwfsSuper_t *ourSuper, const char *title, const FsysRetPtr *list );
extern int mgwfsFindFree(MgwfsSuper_t *ourSuper, MgwfsFoundFreeMap_t *stuff, int numSectors );
extern int mgwfsFreeSectors(MgwfsSuper_t *ourSuper, MgwfsFoundFreeMap_t *stuff, FsysRetPtr *retp);
extern FuseFH_t *getFuseFHidx(MgwfsSuper_t *ourSuper, uint64_t idx);
extern void freeFuseFHidx(MgwfsSuper_t *ourSuper, uint64_t idx);

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
    uint16_t st_sectcyl;
    uint8_t type;
    uint8_t en_head;
    uint16_t en_sectcyl;
    uint32_t abs_sect;
    uint32_t num_sects;
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

/* Funcions in mgwfsf */
extern void displayFileHeader(FILE *outp, FsysHeader *fhp, int retrievalsToo);
extern void displayHomeBlock(FILE *outp, const FsysHomeBlock *homeBlkp, uint32_t cksum);
extern int getHomeBlock(MgwfsSuper_t *ourSuper, uint32_t *lbas, off64_t maxHb, off64_t sizeInSectors, uint32_t *ckSumP);
extern int getFileHeader(const char *title, MgwfsSuper_t *ourSuper, uint32_t id, uint32_t lbas[FSYS_MAX_ALTS], FsysHeader *fhp);
extern int readFile(const char *title,  MgwfsSuper_t *ourSuper, uint8_t *dst, int bytes, FsysRetPtr *retPtr);
extern void dumpIndex(uint32_t *indexBase, int bytes);
extern void dumpFreemap(const char *title, FsysRetPtr *rpBase, int entries );
extern void dumpDir(uint8_t *dirBase, int bytes, MgwfsSuper_t *ourSuper, uint32_t *indexSys );
extern void verifyFreemap(MgwfsSuper_t *ourSuper);
extern int unpackDir(MgwfsSuper_t *ourSuper, MgwfsInode_t *inode, int nest);
extern int tree(MgwfsSuper_t *ourSuper, int topIdx, int nest);
extern int findInode(MgwfsSuper_t *ourSuper, int topIdx, const char *path);

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
	const char *image;
	const char *logFile;
	const char *testPath;
} Options_t;

extern Options_t options;
extern const struct fuse_operations mgwfsf_oper;

#endif /*__MGWFSF_H__*/
