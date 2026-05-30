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
typedef struct
{
	uint32_t sectorsFree;		/* free sectors (from freeMap) */
	uint32_t sectorsUsed;		/* used sectors */
	uint32_t sectorsLost;		/* sectors lost track of */
	int32_t  freeMapEntriesUsed;	/* freemap entries in use */
	int32_t  freeMapEntriesAvail;	/* freemap entries available */
	int32_t  numInodesUsed;		/* inodes in use */
	int32_t  numInodesAvailable;	/* inodes allocated */
	int32_t  numDirtyInodes;	/* inodes pending write-back */
	uint32_t verbose;		/* current verbose flags */
} MgwfsIoctlStats_t;

#define MGWFS_IOC_MAGIC 'M'
#define MGWFS_IOC_GETSTATS	_IOR(MGWFS_IOC_MAGIC, 1, MgwfsIoctlStats_t)
#define MGWFS_IOC_GETVERBOSE	_IOR(MGWFS_IOC_MAGIC, 2, uint32_t)
#define MGWFS_IOC_SETVERBOSE	_IOW(MGWFS_IOC_MAGIC, 3, uint32_t)

#endif /* MGWFS_IOCTL_H_ */
