#ifndef __MGWFSF_H__
#define __MGWFSF_H__

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
#define VERBOSE_ITERATE		(1<<VERB_BIT_ITERATE)	/* iterate directory tree */
#define VERBOSE_FUSE		(1<<VERB_BIT_FUSE)		/* Show fuse stuff */
#define VERBOSE_FUSE_CMD	(1<<VERB_BIT_FUSE_CMD)	/* Show fuse commands */
#define VERBOSE_ANY			((1<<VERB_BIT_MAX)-1)	/* Any verbose bit */

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

#endif /*__MGWFSF_H__*/
