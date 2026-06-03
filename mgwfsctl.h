/*
  mgwfs_ioctl.h: ioctl(2) ABI shared between the mgwfs FUSE daemon and the
  mgwfsctl userland helper. Keep this self-contained (only stdint/ioctl) so
  the helper can include it without dragging in the whole filesystem.

  Copyright (C) 2025  Dave Shepperd <mgwfs@dshepperd.com>
  This program can be distributed under the terms of the GNU GPLv2.
*/

#ifndef MGWFS_IOCTL_H_
#define MGWFS_IOCTL_H_

#include <stdint.h>
#include <sys/ioctl.h>

/* ioctl(2) interface exposed on any file/directory of a mounted mgwfs.
 * Lets a userland tool query live filesystem stats and get/set the
 * verbose flags without remounting. cmd encodes the struct size so libfuse
 * hands us a correctly sized data buffer (no FUSE_IOCTL_UNRESTRICTED retry).
 */
#define MAX_NUM_BOOT_FILES	 (4)
#define MAX_BOOT_FN_PATH	 (128)	/* arbitrary length. Will be plenty long enough for any game produced */
#define MAX_DIRTY_INODE_LIST (4)

typedef struct
{
	uint16_t hbMajor;			/* Major version of home block */
	uint16_t hbMinor;			/* Minor version of home block */
	uint16_t hbSize;			/* size of home block (old versions=80, new versions=152) */
	uint16_t maxAlts;			/* FSYS_MAX_ALTS value on media */
	uint32_t defExtend;			/* default number of sectors to extend */
	uint32_t sectorsFree;		/* free sectors (from freeMap) */
	uint32_t sectorsUsed;		/* used sectors */
	uint32_t sectorsLost;		/* sectors lost track of */
	int32_t  freeMapEntriesUsed;	/* freemap entries in use */
	int32_t  freeMapEntriesAvail;	/* freemap entries available */
	int32_t  numInodesUsed;		/* inodes in use */
	int32_t  numInodesAvailable;	/* inodes allocated */
	int32_t  numDirtyInodes;	/* inodes pending write-back */
	int32_t  listOfDirtyInodes[MAX_DIRTY_INODE_LIST];
	uint32_t verbose;		/* current verbose flags */
	char bootFiles[MAX_NUM_BOOT_FILES][MAX_BOOT_FN_PATH];
} MgwfsIoctlStats_t;

#define MGWFS_IOC_MAGIC 'M'
#define MGWFS_IOC_GETSTATS		_IOR(MGWFS_IOC_MAGIC, 1, MgwfsIoctlStats_t)
#define MGWFS_IOC_GETVERBOSE	_IOR(MGWFS_IOC_MAGIC, 2, uint32_t)
#define MGWFS_IOC_SETVERBOSE	_IOW(MGWFS_IOC_MAGIC, 3, uint32_t)
#define MGWFS_IOC_SETBOOT0		_IOW(MGWFS_IOC_MAGIC, 4, char *)
#define MGWFS_IOC_SETBOOT1		_IOW(MGWFS_IOC_MAGIC, 5, char *)
#define MGWFS_IOC_SETBOOT2		_IOW(MGWFS_IOC_MAGIC, 6, char *)
#define MGWFS_IOC_SETBOOT3		_IOW(MGWFS_IOC_MAGIC, 7, char *)

#endif /* MGWFS_IOCTL_H_ */
