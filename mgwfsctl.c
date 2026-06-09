/*
  mgwfsctl: userland control/query helper for a mounted mgwfs filesystem.

  Issues the mgwfs ioctls (see mgwfs_ioctl.h) against any path inside a
  mounted mgwfs to read live stats or get/set the verbose flags without
  remounting.

  Copyright (C) 2025  Dave Shepperd <mgwfs@dshepperd.com>
  This program can be distributed under the terms of the GNU GPLv2.

  Build: see Makefile target 'mgwfsctl' (it has no fuse dependency).
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE (1)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <getopt.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/vfs.h>
#include <stdint.h>

#include "mgwfsctl.h"

static const char *Prog = "mgwfsctl";

static void usage(FILE *fp, int quietly)
{
	fprintf(fp,
		"Usage: %s [options] <command> <path-in-mount> [arg]\n"
		,Prog );
	if ( !quietly )
	{
		fprintf(fp,
			"  query a mounted mgwfs filesystem via ioctl(2). <path-in-mount> is any\n"
			"  existing file or directory inside the mount point.\n"
			"\n"
			"Optional options:\n"
			" -h or --help                 This message\n"
			" -f                           With 'stats' command, also report returns from statfs()\n"
			"Commands:\n"
			"  stats <path>                print live filesystem statistics\n"
			"  getverbose <path>           print the current verbose flags (hex)\n"
			"  setverbose <path> <v>       set the verbose flags; <v> may be decimal, 0x.. hex, or 0.. octal\n"
			"  setboot <path> [<v>]        set the file pointed to by 'path' to boot image 'v' (v can be 0, 1, 2 or 3, defaults to 0)\n"
			"  checksums <path>            compute checksums and store the results in <path>\n"
			"\n"
			"Examples:\n"
			"  %s stats /mnt/mgw\n"
			"  %s getverbose /mnt/mgw/.\n"
			"  %s setverbose /mnt/mgw 0x40000\n"
			"  %s setboot /mnt/mgw/FOO/bar\n"
			"  %s setboot /mnt/mgw/SOMEWHERE/rainbow 1\n"
			"  %s checksums /mnt/mgw/diags/checksums\n"
			, Prog, Prog, Prog, Prog, Prog, Prog);
	}
}

/* Open a path inside the mount. O_RDONLY is enough; the ioctl does the work. */
static int openPath(const char *path)
{
	int fd = open(path, O_RDONLY);
	if ( fd < 0 )
		fprintf(stderr, "%s: cannot open '%s': %s\n", Prog, path, strerror(errno));
	return fd;
}

static int doStats(const char *path, int doFSToo)
{
	MgwfsIoctlStats_t st;
	int ii,lim;

	int fd = openPath(path);
	if ( fd < 0 )
		return 1;
	if ( doFSToo )
	{
		struct statfs stfs;
		memset(&stfs,0,sizeof(stfs));
		if ( statfs(path, &stfs) >= 0 )
		{
			printf("fs: f_type          : 0x%08lX\n", stfs.f_type);
			printf("fs: f_bsize         : %10" PRIi64 "\n", stfs.f_bsize);
			printf("fs: f_blocks        : %10" PRIi64 "\n", stfs.f_blocks);
			printf("fs: f_bfree         : %10" PRIi64 "\n", stfs.f_bfree);
			printf("fs: f_bavail        : %10" PRIi64 "\n", stfs.f_bavail);
			printf("fs: f_files         : %10" PRIi64 "\n", stfs.f_files);
			printf("fs: f_ffree         : %10" PRIi64 "\n", stfs.f_ffree);
			printf("fs: f_namelen       : %10" PRIi64 "\n", stfs.f_namelen);
			printf("fs: f_frsize        : %10" PRIi64 "\n", stfs.f_frsize);
			printf("fs: f_flags         : 0x%08lX\n", stfs.f_flags);
		}
	}
	memset(&st, 0, sizeof(st));
	if ( ioctl(fd, MGWFS_IOC_GETSTATS, &st) < 0 )
	{
		fprintf(stderr, "%s: MGWFS_IOC_GETSTATS on '%s' failed: %s\n", Prog, path, strerror(errno));
		close(fd);
		return 1;
	}
	close(fd);
	printf("hbMajor             : %" PRIu16 "\n", st.hbMajor);
	if ( st.hbSize == 76 )
		printf("hbMinor             : %" PRIu16 " (actually, it's version 1.0)\n", st.hbMinor);
	else
		printf("hbMinor             : %" PRIu16 "\n", st.hbMinor);
	printf("hbSize              : %" PRIu16 "\n", st.hbSize);
	printf("maxAlts             : %" PRIu16 "\n", st.maxAlts);
	printf("defExtend           : %" PRIu32 "\n", st.defExtend);
	printf("sectorsUsed         : %" PRIu32 "\n", st.sectorsUsed);
	printf("sectorsFree         : %" PRIu32 "\n", st.sectorsFree);
	printf("sectorsLost         : %" PRIu32 "\n", st.sectorsLost);
	printf("freeMapEntriesUsed  : %" PRId32 "\n", st.freeMapEntriesUsed);
	printf("freeMapEntriesAvail : %" PRId32 "\n", st.freeMapEntriesAvail);
	printf("numInodesUsed       : %" PRId32 "\n", st.numInodesUsed);
	printf("numInodesAvailable  : %" PRId32 "\n", st.numInodesAvailable);
	printf("numDirtyInodes      : %" PRId32 "\n", st.numDirtyInodes);
	lim = st.numDirtyInodes;
	if ( lim > MAX_DIRTY_INODE_LIST )
		lim = MAX_DIRTY_INODE_LIST;
	if ( lim )
	{
		printf("    Dirty Inode ID's: ");
		for ( ii = 0; ii < lim; ++ii )
			printf(" %d", st.listOfDirtyInodes[ii]);
		if ( ii < st.numDirtyInodes )
			printf(" (+%d more)", st.numDirtyInodes-ii);
		printf("\n");
	}
	printf("verbose             : 0x%08" PRIx32 "\n", st.verbose);
	if ( st.hbMajor == 1 && st.hbMinor < 3 )
		printf("Version 1.%d and earlier versions of filesystem have boot hardcoded to CODE/vmunix\n", st.hbMinor);
	else 
	{
		if ( st.hbMajor == 1 && st.hbMinor < 6 )
		{
			printf("Boot file           : '%s'\n", st.bootFiles[0]);
		}
		else
		{
			for ( ii = 0; ii < MAX_NUM_BOOT_FILES; ++ii )
				printf("Boot file %d       : '%s'\n", ii, st.bootFiles[ii]);
		}
	}
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

static int doSetBoot(const char *path, int bootNum)
{
	int fd;
	MgwfsIoctlStats_t st;

	errno = 0;
	fd = openPath(path);
	if ( fd < 0 )
		return 1;
	memset(&st, 0, sizeof(st));
	if ( ioctl(fd, MGWFS_IOC_GETSTATS, &st) < 0 )
	{
		fprintf(stderr, "%s: MGWFS_IOC_GETSTATS on '%s' failed: %s\n", Prog, path, strerror(errno));
		close(fd);
		return 1;
	}
	if ( st.hbMajor == 1 && st.hbMinor < 3 )
	{
		fprintf(stderr,"%s: Setting of boot file not supported in filesystem earlier than version 1.3. Boot image is hardcoded to CODE/vmunix\n", Prog);
		close(fd);
		return 1;
	}
	if ( st.hbMajor == 1 && st.hbMinor < 7 && bootNum )
	{
		fprintf(stderr,"%s: Setting of boot file other than entry 0 not supported in filesystem earlier than version 1.7.\n", Prog);
		close(fd);
		return 1;
	}
	if ( ioctl(fd, MGWFS_IOC_SETBOOT0 + bootNum, "") < 0 )
	{
		int bad = errno;
		fprintf(stderr, "%s: MGWFS_IOC_SETBOOT on '%s' failed: (%d) %s\n", Prog, path, errno, strerror(errno));
		if ( bad == EINVAL )
			fprintf(stderr,"%s: Setting of boot file not supported in filesystem earlier than version 1.3. Boot image is hardcoded to CODE/vmunix\n", Prog);
		close(fd);
		return 1;
	}
	close(fd);
	if ( st.hbMajor == 1 && st.hbMinor >= 7 )
		printf("Boot file entry %" PRIx32 " set to '%s'\n", bootNum, path);
	else
		printf("Boot file '%s' set\n", path);
	return 0;
}

static int doChecksums(const char *path)
{
	int v=0,fd;

	errno = 0;
	fd = openPath(path);
	if ( fd < 0 )
		return 1;
	if ( ioctl(fd, MGWFS_IOC_CHECKSUMS, &v) < 0 )
	{
		fprintf(stderr, "%s: MGWFS_IOC_CHECKSUMS on '%s' failed: %s\n", Prog, path, strerror(errno));
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}

typedef enum
{
	OPT_HELP=1,
	OPT_MAX
} Options_t;

static const struct option LongOptions[] =
{
	{ "help", no_argument,	NULL, OPT_HELP },
	{ NULL, 0, NULL, 0 }
};

typedef enum
{
	ARG_CMD,
	ARG_PATH,
	ARG_ARG1,
	ARG_ARG2,
	ARG_ARG3,
	ARG_MAX
} Arguments_t;

int main(int argc, char *argv[])
{
	const char *arguments[ARG_MAX];
	int optArg, doFSToo=0;
	
	if ( argc > 0 && argv[0][0] )
		Prog = argv[0];
	while ( 1 )
	{
		optArg = getopt_long(argc, argv, "hf", LongOptions, NULL);
		switch (optArg)
		{
		case OPT_HELP:
		case 'h':
			usage(stdout, 0);
			return 1;
		case 'f':
			doFSToo = 1;
			break;
		case 0:
			break;
		default:
			if ( optArg < 0 )
				break;
			usage(stderr,1);
			return 1;
		}
		if ( optArg < 0 )
			break;
	}
	memset(arguments,0,sizeof(arguments));
	for (optArg=0; optArg < ARG_MAX; ++optArg)
	{
		if ( argc-optind < 0 )
			break;
		arguments[optArg] = argv[optind];
		++optind;
	}
	if ( !arguments[ARG_CMD] || !arguments[ARG_PATH] )
	{
		fprintf(stderr,"Need both a command and a path\n");
		usage(stderr, 1);
		return 1;
	}
	if ( !strcmp(arguments[ARG_CMD], "stats") )
		return doStats(arguments[ARG_PATH], doFSToo);
	if ( !strcmp(arguments[ARG_CMD], "getverbose") )
		return doGetVerbose(arguments[ARG_PATH]);
	if ( !strcmp(arguments[ARG_CMD], "checksums") )
		return doChecksums(arguments[ARG_PATH]);
	if ( !strcmp(arguments[ARG_CMD], "setverbose") )
	{
		if ( !arguments[ARG_ARG1] )
		{
			fprintf(stderr, "%s: setverbose requires a value\n", Prog);
			usage(stderr, 1);
			return 1;
		}
		return doSetVerbose(arguments[ARG_PATH], arguments[ARG_ARG1]);
	}
	if ( !strcmp(arguments[ARG_CMD], "setboot") )
	{
		int bootNum=0;
		if ( arguments[ARG_ARG1] )
		{
			char *endp = NULL;
			bootNum = strtol(arguments[ARG_ARG1],&endp,0);
			if ( !endp || *endp || bootNum < 0 || bootNum > 3 )
			{
				fprintf(stderr,"Boot number can only be 0, 1, 2 or 3: %s\n", arguments[ARG_ARG1]);
				usage(stderr, 1);
				return 1;
			}
		}
		return doSetBoot(arguments[ARG_PATH], bootNum);
	}
	fprintf(stderr, "%s: unknown command '%s'\n", Prog, arguments[ARG_CMD]);
	usage(stderr, 1);
	return 1;
}
