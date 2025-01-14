#ifndef STANDALONE_FREEMAP
	#define STANDALONE_FREEMAP (0)
#endif

#if !STANDALONE_FREEMAP

	#include "kmgwfs.h"

#else

	#define _LARGEFILE64_SOURCE
	#include <unistd.h>
	#include <stdio.h>
	#include <sys/types.h>
	#include <sys/stat.h>
	#include <fcntl.h>
	#include <stdint.h>
	#include <stdlib.h>
	#include <string.h>
	#include <errno.h>
	#include <getopt.h>
	#include <ctype.h>
typedef uint32_t sector_t;
	#include "mgwfs.h"
	#define pr_info printf
	#define pr_err  printf

#endif	/* STANDALONE_FREEMAP */

void mgwfsDumpFreeMap(const char *title, const FsysRetPtr *list)
{
	int ii = 0;
	if ( title )
		pr_info("mgwfs_dumpfree(): %s\n", title);
	while ( list->start )
	{
		pr_info("mgwfs_dumpfree(): %3d: 0x%08X-0x%08X (0x%X)\n",
				ii, list->start, list->start + list->nblocks - 1, list->nblocks);
		++ii;
		++list;
	}
}

int mgwfsFindFree(MgwfsSuper_t *ourSuper, MgwfsFoundFreeMap_t *stuff, int numSectors)
{
	if ( stuff )
	{
		int ii, leastDiff, leastDiffIdx;
		FsysRetPtr *src;

		/* assume nothing to report */
		stuff->result.nblocks = 0;
		stuff->result.start = 0;
		if ( stuff->hint.start && stuff->hint.start )
		{
			uint32_t contigiousSector = stuff->hint.start + stuff->hint.nblocks;
			/* A hint was provided. So see if there is a connecting section */
			if ( (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_FREE) )
			{
				pr_info("mgwfsFindFree(): Looking for 0x%X sector%s. Hint=0x%08X/0x%X. Contigious sector=0x%08X\n",
						numSectors, numSectors == 1 ? "" : "s", stuff->hint.start, stuff->hint.nblocks, contigiousSector);
			}
			src = ourSuper->freeMap;
			for ( ii = 0; ii < stuff->currListAlloc; ++ii, ++src )
			{
				if ( !src->start || !src->nblocks )
					break;
				if ( src->start == contigiousSector  )
				{
					int num = numSectors;
					if ( num > src->nblocks )
						num = src->nblocks;
					/* we found a connecting section */
					/* we can just clip off sectors from the current section */
					stuff->result.start = src->start;
					stuff->result.nblocks = num;
					src->start += num;
					if ( !(src->nblocks -= num) )
					{
						/* remove the existing section completely */
						memmove(src, src + 1, (stuff->currListAlloc - ii) * sizeof(FsysRetPtr));
						memset(ourSuper->freeMap + stuff->currListAlloc - 1, 0, sizeof(FsysRetPtr));
					}
					stuff->updatedEntryIndex = ii;
					ourSuper->freeListDirty = 1;
					return 1;   /* something changed */
				}
			}
		}
		/* Did not find anything we can add contigious to an existing section */
		if ( (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_FREE) )
		{
			pr_info("mgwfsFindFree(): Looking for 0x%X sector%s from anywhere.\n",
					numSectors, numSectors == 1 ? "" : "s");
		}
		src = ourSuper->freeMap;
		for ( ii = 0; ii < stuff->currListAlloc; ++ii, ++src )
		{
			if ( !src->start )
				break;
			if ( src->nblocks == numSectors )
			{
				/* we found a section with the requested number of sectors exactly */
				/* we can just clip off all of the new sectors from the current section */
				stuff->result.start = src->start;
				stuff->result.nblocks = numSectors;
				/* remove the existing section completely */
				memmove(src, src + 1, (stuff->currListAlloc - ii) * sizeof(FsysRetPtr));
				memset(ourSuper->freeMap + stuff->currListAlloc - 1, 0, sizeof(FsysRetPtr));
				stuff->updatedEntryIndex = ii;
				ourSuper->freeListDirty = 1;
				return 1;   /* something changed */
			}
		}
		src = ourSuper->freeMap;
		for ( ii = 0; ii < stuff->currListAlloc; ++ii, ++src )
		{
			if ( !src->start )
				break;
			if ( src->nblocks > numSectors )
			{
				/* we found a section with at least the requested number of sectors */
				/* we can just clip off the new sectors from the current section */
				stuff->result.start = src->start;
				stuff->result.nblocks = numSectors;
				src->start += numSectors;
				src->nblocks -= numSectors;
				stuff->updatedEntryIndex = ii;
				ourSuper->freeListDirty = 1;
				return 1;   /* something changed */
			}
		}
		src = ourSuper->freeMap;
		leastDiffIdx = 0;
		leastDiff = 0x00FFFFFF;
		for ( ii = 0; ii < stuff->currListAlloc; ++ii, ++src )
		{
			if ( !src->start )
				break;
			if ( numSectors - src->nblocks < leastDiff )
			{
				leastDiff = numSectors - src->nblocks;
				leastDiffIdx = ii;
			}
		}
		if ( (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_FREE) )
			pr_info("mgwfsFindFree(): Found leastDiff=0x%X, leastDiffIdx=%d\n", leastDiff, leastDiffIdx);
		if ( leastDiff < 0x00FFFFFF )
		{
			src = ourSuper->freeMap + leastDiffIdx;
			stuff->result.start = src->start;
			stuff->result.nblocks = src->nblocks;
			/* remove the found section completely */
			memmove(src, src + 1, (stuff->currListAlloc - leastDiffIdx) * sizeof(FsysRetPtr));
			memset(ourSuper->freeMap + stuff->currListAlloc - 1, 0, sizeof(FsysRetPtr));
			stuff->updatedEntryIndex = leastDiffIdx;
			ourSuper->freeListDirty = 1;
			return 1;   /* something changed */
		}
	}
	return 0;
}

#define HANDLE_FREE_OVERLAPS (0)	/* Write this code someday */

int mgwfsFreeSectors(MgwfsSuper_t *ourSuper, MgwfsFoundFreeMap_t *stuff, FsysRetPtr *retp)
{
	if ( stuff && retp )
	{
		int ii;
		FsysRetPtr *src;
		FsysRetPtr *src1;
		uint32_t endRetSector = retp->start + retp->nblocks;

		/* Look through the list to see if there is a connection front or back */
		src = ourSuper->freeMap;
		if ( (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_FREE) )
			pr_info("mgwsFreeSectors(): Freeing 0x%08X-0x%08X (0x%X) sectors\n", retp->start, retp->start + retp->nblocks - 1, retp->nblocks);
		for ( ii = 0; src->start && src->nblocks && ii < stuff->currListAlloc; ++ii, ++src )
		{
			uint32_t srcEnd;

			/* If the last sector to free is below the current, we're done looking. */
			if ( endRetSector < src->start )
				break;
			srcEnd = src->start + src->nblocks;
			/* Point to next one on the list */
			src1 = src + 1;
#if !HANDLE_FREE_OVERLAPS
			if ( endRetSector == src->start )
			{
				/* We are to free ahead of current */
				src->start = retp->start;
				src->nblocks += retp->nblocks;
				ourSuper->freeListDirty = 1;
				stuff->updatedEntryIndex = ii;
				if ( (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_FREE) )
					pr_info("mgwsFreeSectors(): Free'd in front of entry %d to 0x%08X-0x%08X (0x%X)\n", ii, src->start, src->start + src->nblocks - 1, src->nblocks);
				return 1;
			}
			if ( retp->start == srcEnd )
			{
				if ( src1->start && src1->start < endRetSector )
				{
					pr_err("mgwsFreeSectors(): BUG: Tried to free 0x%08X-0x%08X (%d) which overlaps entry %d: 0x%08X-0x%08X (%d)\n",
						   retp->start, retp->start + retp->nblocks - 1, retp->nblocks,
						   ii + 1, src1->start, src1->start + src1->nblocks - 1, src1->nblocks);
					return 0;
				}
				/* We are to free behind current */
				src->nblocks += retp->nblocks;
				if ( src1->start && src1->start == endRetSector )
				{
					int numMove;
					/* The free connected two sections so join them together */
					if ( (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_FREE) )
						pr_info("mgwsFreeSectors(): Joined adjacent entries %d 0x%08X-0x%08X and %d 0x%08X-0x%08X to 0x%08X-0x%08X\n",
								ii, src->start, src->start + src->nblocks - 1,
								ii + 1, src1->start, src1->start + src1->nblocks - 1,
								src->start, src->start + src->nblocks + src1->nblocks - 1);
					src->nblocks += src1->nblocks;
					numMove = stuff->currListAlloc - ii - 2;
					/* Delete an entry as long as it's not the last */
					if ( numMove > 0 )
						memmove(src1, src1 + 1, numMove * sizeof(FsysRetPtr));
					src1 = ourSuper->freeMap + stuff->currListAlloc - 1;
					/* The last one gets 0's */
					src1->start = 0;
					src1->nblocks = 0;
				}
				ourSuper->freeListDirty = 1;
				stuff->updatedEntryIndex = ii;
				if ( (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_FREE) )
					pr_info("mgwsFreeSectors(): Free'd behind entry %d to 0x%08X-0x%08X (0x%X)\n", ii, src->start, src->start + src->nblocks - 1, src->nblocks);
				return 1;
			}
			if ( retp->start > srcEnd )
				continue;               /* this should be the normal condition */
			/* Overlap conditions should be recorded as a bug instead of handled */
			pr_err("mgwsFreeSectors(): BUG: Tried to free 0x%08X-0x%08X (0x%X) which overlaps entry %d: 0x%08X-0x%08X (0x%X)\n",
				   retp->start, retp->start + retp->nblocks - 1, retp->nblocks,
				   ii, src->start, src->start + src->nblocks - 1, src->nblocks);
			return 0;
#else
			/* Write this code someday */
#endif
		}
		/* Did not find anything so we need to insert a new entry at entry ii */
		if ( ii >= stuff->currListAlloc )
		{
			pr_err("mgwsFreeSectors(): No room to add new free entry. ii=%d, currListAlloc=%d\n",
				   ii, stuff->currListAlloc);
			return 0;
		}
		src = ourSuper->freeMap + ii;
		memmove(src + 1, src, (stuff->currListAlloc - ii) * sizeof(FsysRetPtr));
		src->start = retp->start;
		src->nblocks = retp->nblocks;
		ourSuper->freeListDirty = 1;
		stuff->addedEntryIndex = ii;
		if ( (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_FREE) )
			pr_info("mgwsFreeSectors(): Added new last %d of 0x%08X-0x%08X (0x%X)\n", ii, src->start, src->start + src->nblocks - 1, src->nblocks);
		return 1;
	}
	pr_err("mgwsFreeSectors(): Not enough parameters provided. stuff=%p, retp=%p\n", (void *)stuff, (void *)retp);
	return 0;
}

#if STANDALONE_FREEMAP
static FsysRetPtr sampleFreeMap[] =
{
	{ 0x1000, 10 },
	{ 0x1020, 20 },
	{ 0x1100, 100 },
	{ 0x1300, 1000 },
	{ 0x2400, 1 },
	{ 0x2500, 5 },
	{ 0, 0 },
	{ 0, 0 }
};

static int help_em(const char *title)
{
	printf("%s [-v][-s sector][-r sector[,num]] numSectors\n"
		   "Where:\n"
		   "-r sector[,num] = specify sector and optional number of sectors to return to freelist\n"
		   "-s sector       = specify a sector hint\n"
		   "-v              = set verbose mode\n"
		   , title);
	return 1;
}

int main(int argc, char **argv)
{
	int opt;
	int numSectors, allocated;
	char *endp;
	FsysRetPtr retSec;
	MgwfsFoundFreeMap_t found;
	MgwfsSuper_t super;

	memset(&found, 0, sizeof(found));
	memset(&super, 0, sizeof(super));
	retSec.nblocks = 0;
	retSec.start = 0;
	while ( (opt = getopt(argc, argv, "r:s:v")) != -1 )
	{
		switch (opt)
		{
		case 'r':
			endp = NULL;
			retSec.start = strtol(optarg, &endp, 0);
			retSec.nblocks = 1;
			if ( endp && *endp == ',' )
			{
				char *numSec = endp + 1;
				endp = NULL;
				retSec.nblocks = strtol(numSec, &endp, 0);
			}
			if ( !endp || *endp || retSec.start < 4 || retSec.start + retSec.nblocks >= 0x00FFFFFF )
			{
				fprintf(stderr, "Bad argument for -r: '%s'\n", optarg);
				return 1;
			}
			break;
		case 's':
			endp = NULL;
			found.hint.start = strtol(optarg, &endp, 0);
			if ( !endp || *endp || found.hint.start < 4 || found.hint.start >= 0x00FFFFFF )
			{
				fprintf(stderr, "Bad argument for -s: '%s'\n", optarg);
				return 1;
			}
			break;
		case 'v':
			super.flags = MGWFS_MNT_OPT_VERBOSE_FREE;
			break;
		default:
			fprintf(stderr, "Undefined command line arg: '%c'(%d)\n", isprint(opt) ? opt : '.', opt);
			return help_em(argv[0]);
		}
	}
/*	printf("argc=%d, optind=%d\n", argc, optind); */
	if ( argc - optind < 1 )
		return help_em(argv[0]);
	endp = NULL;
	numSectors = strtol(argv[optind], &endp, 0);
	if ( !endp || *endp || numSectors < 1 || numSectors >= 0x00FFFFFF )
	{
		fprintf(stderr, "Bad argument for -n: '%s'\n", argv[optind]);
		return 1;
	}
	found.currListAlloc = sizeof(sampleFreeMap) / sizeof(FsysRetPtr);
	super.freeMap = sampleFreeMap;
	if ( super.flags )
		mgwfsDumpFreeMap("Before:", sampleFreeMap);
	allocated = 0;
	if ( retSec.start )
	{
		mgwfsFreeSectors(&super, &found, &retSec);
		if ( super.flags )
			mgwfsDumpFreeMap("After Free:", sampleFreeMap);
	}
	while ( allocated < numSectors )
	{
		if ( mgwfsFindFree(&super, &found, numSectors) )
		{
			allocated += found.result.nblocks;
			if ( super.flags )
			{
				printf("Asked for %d sector%s. (hint=0x%08X) Found %d at 0x%08X (updIndex=%d, addedIdx=%d, allocated=%d)\n",
					   numSectors,
					   numSectors == 1 ? "" : "s",
					   found.hint.start,
					   found.result.nblocks,
					   found.result.start,
					   found.updatedEntryIndex,
					   found.addedEntryIndex,
					   allocated);
			}
			found.hint.start = 0;
		}
		else
		{
			printf("Did not find any %ssectors to add\n", allocated ? "more " : "");
			break;
		}
	}
	if ( super.flags )
	{
		printf("Total allocated: %d\n", allocated);
		mgwfsDumpFreeMap("After:", sampleFreeMap);
	}
	return 0;
}

#endif	/* STANDALONE_FREELIST */
