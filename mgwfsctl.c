/*
  mgwfsctl: userland control/query helper for a mounted mgwfs filesystem.

  Issues the mgwfs ioctls (see mgwfs_ioctl.h) against any path inside a
  mounted mgwfs to read live stats or get/set the verbose flags without
  remounting.

  Copyright (C) 2025  Dave Shepperd <mgwfs@dshepperd.com>
  This program can be distributed under the terms of the GNU GPLv2.

  Build: see Makefile target 'mgwfsctl' (it has no fuse dependency).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>

#include "mgwfs_ioctl.h"

static const char *Prog = "mgwfsctl";

static void usage(FILE *fp)
{
	fprintf(fp,
		"Usage: %s <command> <path-in-mount> [arg]\n"
		"  query a mounted mgwfs filesystem via ioctl(2). <path-in-mount> is any\n"
		"  existing file or directory inside the mount point.\n"
		"\n"
		"Commands:\n"
		"  stats <path>          print live filesystem statistics\n"
		"  getverbose <path>     print the current verbose flags (hex)\n"
		"  setverbose <path> <v> set the verbose flags; <v> may be decimal, 0x.. hex, or 0.. octal\n"
		"\n"
		"Examples:\n"
		"  %s stats /mnt/mgwfs\n"
		"  %s getverbose /mnt/mgwfs/.\n"
		"  %s setverbose /mnt/mgwfs 0x40000\n"
		, Prog, Prog, Prog, Prog);
}

/* Open a path inside the mount. O_RDONLY is enough; the ioctl does the work. */
static int openPath(const char *path)
{
	int fd = open(path, O_RDONLY);
	if ( fd < 0 )
		fprintf(stderr, "%s: cannot open '%s': %s\n", Prog, path, strerror(errno));
	return fd;
}

static int doStats(const char *path)
{
	MgwfsIoctlStats_t st;
	int fd = openPath(path);
	if ( fd < 0 )
		return 1;
	memset(&st, 0, sizeof(st));
	if ( ioctl(fd, MGWFS_IOC_GETSTATS, &st) < 0 )
	{
		fprintf(stderr, "%s: MGWFS_IOC_GETSTATS on '%s' failed: %s\n", Prog, path, strerror(errno));
		close(fd);
		return 1;
	}
	close(fd);
	printf("sectorsFree         : %" PRIu32 "\n", st.sectorsFree);
	printf("sectorsUsed         : %" PRIu32 "\n", st.sectorsUsed);
	printf("sectorsLost         : %" PRIu32 "\n", st.sectorsLost);
	printf("freeMapEntriesUsed  : %" PRId32 "\n", st.freeMapEntriesUsed);
	printf("freeMapEntriesAvail : %" PRId32 "\n", st.freeMapEntriesAvail);
	printf("numInodesUsed       : %" PRId32 "\n", st.numInodesUsed);
	printf("numInodesAvailable  : %" PRId32 "\n", st.numInodesAvailable);
	printf("numDirtyInodes      : %" PRId32 "\n", st.numDirtyInodes);
	printf("verbose             : 0x%08" PRIx32 "\n", st.verbose);
	return 0;
}

static int doGetVerbose(const char *path)
{
	uint32_t v = 0;
	int fd = openPath(path);
	if ( fd < 0 )
		return 1;
	if ( ioctl(fd, MGWFS_IOC_GETVERBOSE, &v) < 0 )
	{
		fprintf(stderr, "%s: MGWFS_IOC_GETVERBOSE on '%s' failed: %s\n", Prog, path, strerror(errno));
		close(fd);
		return 1;
	}
	close(fd);
	printf("0x%08" PRIx32 "\n", v);
	return 0;
}

static int doSetVerbose(const char *path, const char *valStr)
{
	char *endp;
	unsigned long val;
	uint32_t v;
	int fd;

	errno = 0;
	val = strtoul(valStr, &endp, 0);	/* base 0: accepts 0x.. , 0.. , decimal */
	if ( errno || endp == valStr || *endp )
	{
		fprintf(stderr, "%s: invalid verbose value '%s'\n", Prog, valStr);
		return 1;
	}
	v = (uint32_t)val;
	fd = openPath(path);
	if ( fd < 0 )
		return 1;
	if ( ioctl(fd, MGWFS_IOC_SETVERBOSE, &v) < 0 )
	{
		fprintf(stderr, "%s: MGWFS_IOC_SETVERBOSE on '%s' failed: %s\n", Prog, path, strerror(errno));
		close(fd);
		return 1;
	}
	close(fd);
	printf("verbose set to 0x%08" PRIx32 "\n", v);
	return 0;
}

int main(int argc, char *argv[])
{
	const char *cmd;

	if ( argc > 0 && argv[0][0] )
		Prog = argv[0];
	if ( argc >= 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "help")) )
	{
		usage(stdout);
		return 0;
	}
	if ( argc < 3 )
	{
		usage(stderr);
		return 1;
	}
	cmd = argv[1];
	if ( !strcmp(cmd, "stats") )
		return doStats(argv[2]);
	if ( !strcmp(cmd, "getverbose") )
		return doGetVerbose(argv[2]);
	if ( !strcmp(cmd, "setverbose") )
	{
		if ( argc < 4 )
		{
			fprintf(stderr, "%s: setverbose requires a value\n", Prog);
			usage(stderr);
			return 1;
		}
		return doSetVerbose(argv[2], argv[3]);
	}
	fprintf(stderr, "%s: unknown command '%s'\n", Prog, cmd);
	usage(stderr);
	return 1;
}
