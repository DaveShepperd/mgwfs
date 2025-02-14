/*
  freemap: Part of Atari/MidwayGamesWest filesystem using libfuse: Filesystem in Userspace

  Copyright (C) 2025  Dave Shepperd <mgwfsf@dshepperd.com>

  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.

 Compile with:
 gcc -Wall -c -g freemap.c 

*/
#ifndef STANDALONE_FREEMAP
	#define STANDALONE_FREEMAP (0)
#endif
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
#include "mgwfsf.h"

void mgwfsDumpFreeMap(MgwfsSuper_t *ourSuper, const char *title, const FsysRetPtr *list)
{
	int ii = 0;
	if ( title )
		fprintf(ourSuper->logFile,"mgwfs_dumpfree(): %s\n", title);
	while ( list->start )
	{
		fprintf(ourSuper->logFile,"mgwfs_dumpfree(): %3d: 0x%08X-0x%08X (0x%X)\n",
				ii, list->start, list->start + list->nblocks - 1, list->nblocks);
		++ii;
		++list;
	}
	if ( !ii )
		fprintf(ourSuper->logFile,"mgwfs_dumpfree(): %3d: <empty>\n", ii);
}

int mgwfsFindFree(MgwfsSuper_t *ourSuper, MgwfsFoundFreeMap_t *stuff, int numSectors)
{
	if ( stuff )
	{
		int ii, leastDiff, leastDiffIdx;
		FsysRetPtr *src, *src1, *hints;
		uint32_t minSector;
		
		/* assume nothing to report */
		stuff->result.nblocks = 0;
		stuff->result.start = 0;
		stuff->allocChange = 0;
		minSector = stuff->minSector;
		if ( (hints=stuff->hints) )
		{
			int hintIdx;
			for ( hintIdx=0; hintIdx < FSYS_MAX_FHPTRS && hints->start && hints->nblocks; ++hintIdx, ++hints )
			{
				uint32_t contigiousSector = hints->start + hints->nblocks;
				/* A hint was provided. So see if there is a connecting section */
				if ( (ourSuper->verbose & VERBOSE_FREE) )
				{
					fprintf(ourSuper->logFile,"mgwfsFindFree(): Looking for 0x%X (%d) sector%s. Hint=0x%08X/0x%X. Contigious sector=0x%08X\n",
							numSectors, numSectors, numSectors == 1 ? "" : "s", hints->start, hints->nblocks, contigiousSector);
				}
				src = ourSuper->freeMap;
				for ( ii = 0; ii < stuff->listUsed; ++ii, ++src )
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
							memmove(src, src + 1, (stuff->listUsed - ii) * sizeof(FsysRetPtr));
							memset(ourSuper->freeMap + stuff->listUsed - 1, 0, sizeof(FsysRetPtr));
							--stuff->allocChange;
							--stuff->listUsed;
						}
						ourSuper->freeListDirty = 1;
						return 1;   /* something changed */
					}
				}
			}
		}
		/* Did not find anything we can add contigious to an existing section */
		if ( (ourSuper->verbose & VERBOSE_FREE) )
		{
			char txt[128];
			if ( minSector )
				snprintf(txt, sizeof(txt), "from at least minSector=0x%08X", minSector);
			else
				strncpy(txt,"from anywhere",sizeof(txt));
			fprintf(ourSuper->logFile,"mgwfsFindFree(): Looking for 0x%X (%d) sector%s %s\n",
					numSectors, numSectors, numSectors == 1 ? "" : "s", txt);
		}
		src = ourSuper->freeMap;
		for ( ii = 0; ii < stuff->listUsed; ++ii, ++src )
		{
			if ( !src->start )
				break;
			if ( minSector && minSector >= src->start + src->nblocks )
				continue;
			if ( src->nblocks == numSectors )
			{
				if ( minSector && minSector != src->start )
					continue;
				/* we found a section with the requested number of sectors exactly */
				/* we can just clip off all of the new sectors from the current section */
				stuff->result.start = src->start;
				stuff->result.nblocks = numSectors;
				/* remove the existing section completely */
				memmove(src, src + 1, (stuff->listUsed - ii) * sizeof(FsysRetPtr));
				memset(ourSuper->freeMap + stuff->listUsed - 1, 0, sizeof(FsysRetPtr));
				--stuff->allocChange;
				--stuff->listUsed;
				ourSuper->freeListDirty = 1;
				return 1;   /* something changed */
			}
		}
		src = ourSuper->freeMap;
		for ( ii = 0; ii < stuff->listUsed; ++ii, ++src )
		{
			if ( !src->start )
				break;
			if ( minSector && minSector >= src->start+src->nblocks )
				continue;
			if ( minSector && minSector >= src->start )
			{
				uint32_t tnblocks;
				tnblocks = src->nblocks - (minSector - src->start);
				if ( tnblocks >= numSectors )
				{
					/* we found a section with at least the requested number of sectors */
					if ( minSector == src->start )
					{
						/* we can just clip off the new sectors from the current section */
						src->start += numSectors;
						src->nblocks -= numSectors;
					}
					else
					{
						if ( minSector + numSectors != src->start + src->nblocks )
						{
							if ( stuff->listUsed >= stuff->listAvailable )
							{
								/* Technically we could just expand the map file. But for ease here, we just say, sorry, no room */
								return 0;
							}
							/* we have to insert an entry */
							src1 = src + 1;
							/* make room for one more */
							memmove(src1, src, (stuff->listUsed - ii - 1) * sizeof(FsysRetPtr));
							/* Leave start as is but reduce size of area in front */
							src1->start = minSector+numSectors;
							src1->nblocks = (src->start + src->nblocks) - (minSector+numSectors);
							src->nblocks = minSector-src->start;
							++stuff->allocChange;
							++stuff->listUsed;
						}
						else
						{
							src->nblocks -= numSectors;
						}
					}
					stuff->result.start = minSector;
					stuff->result.nblocks = numSectors;
					ourSuper->freeListDirty = 1;
					return 1;   /* something changed */
				}
			}
			else
			{
				if ( src->nblocks >= numSectors )
				{
					/* we found a section with at least the requested number of sectors */
					/* we can just clip off the new sectors from the current section */
					stuff->result.start = src->start;
					stuff->result.nblocks = numSectors;
					src->start += numSectors;
					src->nblocks -= numSectors;
					ourSuper->freeListDirty = 1;
					return 1;   /* something changed */
				}
			}
		}
		if ( minSector )
		{
			/* Didn't find anything close, so try the whole thing again without a minSector */
			if ( (ourSuper->verbose & VERBOSE_FREE) )
				fprintf(ourSuper->logFile,"mgwfs_findfree(): Did not find anything on or after 0x%08X of the right size. Trying again without minSector ...\n", minSector);
			stuff->minSector = 0;
			return mgwfsFindFree(ourSuper,stuff,numSectors);
		}
		src = ourSuper->freeMap;
		leastDiffIdx = 0;
		leastDiff = 0x00FFFFFF;
		for ( ii = 0; ii < stuff->listUsed; ++ii, ++src )
		{
			if ( !src->start )
				break;
			if ( numSectors - src->nblocks < leastDiff )
			{
				leastDiff = numSectors - src->nblocks;
				leastDiffIdx = ii;
			}
		}
		if ( (ourSuper->verbose & VERBOSE_FREE) )
			fprintf(ourSuper->logFile,"mgwfsFindFree(): Found leastDiff=0x%X, leastDiffIdx=%d\n", leastDiff, leastDiffIdx);
		if ( leastDiff < 0x00FFFFFF )
		{
			src = ourSuper->freeMap + leastDiffIdx;
			stuff->result.start = src->start;
			stuff->result.nblocks = src->nblocks;
			/* remove the found section completely */
			memmove(src, src + 1, (stuff->listUsed - leastDiffIdx) * sizeof(FsysRetPtr));
			memset(ourSuper->freeMap + stuff->listUsed - 1, 0, sizeof(FsysRetPtr));
			ourSuper->freeListDirty = 1;
			--stuff->allocChange;
			--stuff->listUsed;
			return 1;   /* something changed */
		}
	}
	return 0;
}

#define HANDLE_FREE_OVERLAPS (0)

int mgwfsFreeSectors(MgwfsSuper_t *ourSuper, MgwfsFoundFreeMap_t *stuff, FsysRetPtr *retp)
{
	if ( ourSuper && stuff && retp )
	{
		int ii;
		FsysRetPtr *src;
		FsysRetPtr *src1;
		uint32_t endRetSector = retp->start + retp->nblocks;

		stuff->allocChange = 0;
		/* Look through the list to see if there is a connection front or back */
		src = ourSuper->freeMap;
		if ( (ourSuper->verbose & VERBOSE_FREE) )
			fprintf(ourSuper->logFile,"mgwsFreeSectors(): Freeing 0x%08X-0x%08X (0x%X) sectors\n", retp->start, retp->start + retp->nblocks - 1, retp->nblocks);
		for ( ii = 0; src->start && src->nblocks && ii < stuff->listUsed; ++ii, ++src )
		{
			uint32_t srcEnd;

			/* If the last sector to free is below the current, we're done looking. */
			if ( endRetSector < src->start )
				break;
			srcEnd = src->start + src->nblocks;
			/* Point to next one on the list */
			src1 = src + 1;
			if ( endRetSector == src->start )
			{
				/* We are to free ahead of current */
				src->start = retp->start;
				src->nblocks += retp->nblocks;
				ourSuper->freeListDirty = 1;
				if ( (ourSuper->verbose & VERBOSE_FREE) )
					fprintf(ourSuper->logFile,"mgwsFreeSectors(): Free'd in front of entry %d to 0x%08X-0x%08X (0x%X)\n", ii, src->start, src->start + src->nblocks - 1, src->nblocks);
				return 1;
			}
			if ( retp->start == srcEnd )
			{
				if ( src1->start && src1->start < endRetSector )
				{
					fprintf(stderr,"mgwsFreeSectors(): BUG: Tried to free 0x%08X-0x%08X (%d) which overlaps entry %d: 0x%08X-0x%08X (%d)\n",
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
					if ( (ourSuper->verbose & VERBOSE_FREE) )
						fprintf(ourSuper->logFile,"mgwsFreeSectors(): Joined adjacent entries %d 0x%08X-0x%08X and %d 0x%08X-0x%08X to 0x%08X-0x%08X\n",
								ii, src->start, src->start + src->nblocks - 1,
								ii + 1, src1->start, src1->start + src1->nblocks - 1,
								src->start, src->start + src->nblocks + src1->nblocks - 1);
					src->nblocks += src1->nblocks;
					numMove = stuff->listUsed - ii - 2;
					/* Delete an entry as long as it's not the last */
					if ( numMove > 0 )
					{
						memmove(src1, src1 + 1, numMove * sizeof(FsysRetPtr));
						--stuff->allocChange;
						--stuff->listUsed;
					}
					src1 = ourSuper->freeMap + stuff->listUsed - 1;
					/* The last one gets 0's */
					src1->start = 0;
					src1->nblocks = 0;
				}
				ourSuper->freeListDirty = 1;
				if ( (ourSuper->verbose & VERBOSE_FREE) )
					fprintf(ourSuper->logFile,"mgwsFreeSectors(): Free'd behind entry %d to 0x%08X-0x%08X (0x%X)\n", ii, src->start, src->start + src->nblocks - 1, src->nblocks);
				return 1;
			}
			if ( retp->start > srcEnd )
				continue;               /* this should be the normal condition */
			/* Overlap conditions should be recorded as a bug instead of being handled */
			fprintf(stderr,"mgwsFreeSectors(): BUG: Tried to free 0x%08X-0x%08X (0x%X) which overlaps entry %d: 0x%08X-0x%08X (0x%X)\n",
				   retp->start, retp->start + retp->nblocks - 1, retp->nblocks,
				   ii, src->start, src->start + src->nblocks - 1, src->nblocks);
#if !HANDLE_FREE_OVERLAPS
			return 0;
#else
			/* Write this code someday */
#endif
		}
		/* Did not find anything so we need to insert a new entry at entry ii */
		if ( ii >= stuff->listAvailable )
		{
			fprintf(stderr,"mgwsFreeSectors(): No room to add new free entry. ii=%d, listAvailable=%d\n",
				   ii, stuff->listAvailable);
			return 0;
		}
		if ( stuff->listUsed >= stuff->listAvailable )
		{
			/* technically we could expand the freemap file (buffer), but for now, just say no room */
			return 0;
		}
		src = ourSuper->freeMap + ii;
		memmove(src + 1, src, (stuff->listUsed - ii) * sizeof(FsysRetPtr));
		src->start = retp->start;
		src->nblocks = retp->nblocks;
		ourSuper->freeListDirty = 1;
		++stuff->allocChange;
		++stuff->listUsed;
		if ( (ourSuper->verbose & VERBOSE_FREE) )
			fprintf(ourSuper->logFile,"mgwsFreeSectors(): Added new last %d of 0x%08X-0x%08X (0x%X)\n", ii, src->start, src->start + src->nblocks - 1, src->nblocks);
		return 1;
	}
	fprintf(stderr,"mgwsFreeSectors(): Not enough parameters provided. stuff=%p, retp=%p\n", (void *)stuff, (void *)retp);
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
	{ 0x3000, 10000 },
	{ 0, 0 },
	{ 0, 0 }
};

static int help_em(const char *title)
{
	printf("%s [-v][-c count][-m min][-s sector][-r sector[,num]] numSectors\n"
		   "Where:\n"
		   "-c count        = specify the count of sections to get\n"
		   "-m min          = specify the minimum sector to look for\n"
		   "-r sector[,num] = specify sector and optional number of sectors to return to freelist\n"
		   "-s sector       = specify a sector hint\n"
		   "-v              = set verbose mode\n"
		   , title);
	return 1;
}

#define MAX_RESULTS (10)

int main(int argc, char **argv)
{
	int opt, alts=1, resIdx, allocChange;
	int numSectors, allocated;
	char *endp;
	FsysRetPtr retSec, hints[FSYS_MAX_FHPTRS];
	MgwfsFoundFreeMap_t found;
	MgwfsSuper_t super;
	FsysRetPtr results[MAX_RESULTS], *rp;
	
	memset(&found, 0, sizeof(found));
	memset(&super, 0, sizeof(super));
	memset(&hints, 0, sizeof(hints));
	memset(results,0, sizeof(results));
	found.hints = hints;
	retSec.nblocks = 0;
	retSec.start = 0;
	super.logFile = stdout;
	while ( (opt = getopt(argc, argv, "c:m:r:s:v")) != -1 )
	{
		switch (opt)
		{
		case 'c':
			endp = NULL;
			alts = strtol(optarg, &endp, 0);
			if ( !endp || *endp || alts < 1 || alts > FSYS_MAX_ALTS )
			{
				fprintf(stderr, "Bad argument for -c: '%s'\n", optarg);
				return 1;
			}
			break;
			
		case 'm':
			endp = NULL;
			found.minSector = strtol(optarg, &endp, 0);
			if ( !endp || *endp || found.minSector < 0 || found.minSector >= 0x00FFFFFF )
			{
				fprintf(stderr, "Bad argument for -m: '%s'\n", optarg);
				return 1;
			}
			break;

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
			found.hints[0].start = strtol(optarg, &endp, 0);
			if ( !endp || *endp || found.hints[0].start < 4 || found.hints[0].start >= 0x00FFFFFF )
			{
				fprintf(stderr, "Bad argument for -s: '%s'\n", optarg);
				return 1;
			}
			break;
		case 'v':
			super.verbose = VERBOSE_FREE;
			break;
		default:
			fprintf(stderr, "Undefined command line arg: '%c'(%d)\n", isprint(opt) ? opt : '.', opt);
			return help_em(argv[0]);
		}
	}
/*	printf("argc=%d, optind=%d\n", argc, optind); */
	if ( argc - optind < 1 )
		return help_em(argv[0]);
	if ( alts > 1 && found.minSector )
	{
		fprintf(stderr,"Cannot provide both -c and -m\n");
		return help_em(argv[0]);
	}
	endp = NULL;
	numSectors = strtol(argv[optind], &endp, 0);
	if ( !endp || *endp || numSectors < 1 || numSectors >= 0x00FFFFFF )
	{
		fprintf(stderr, "Bad argument for number of sectors: '%s'\n", argv[optind]);
		return 1;
	}
	super.freeMap = sampleFreeMap;
	rp = sampleFreeMap;
	while ( rp->nblocks )
		++rp;
	found.listAvailable = sizeof(sampleFreeMap) / sizeof(FsysRetPtr);
	found.listUsed = rp-sampleFreeMap;
	if ( super.verbose )
		mgwfsDumpFreeMap(&super,"Freelist Before:", sampleFreeMap);
	allocated = 0;
	if ( retSec.start )
	{
		mgwfsFreeSectors(&super, &found, &retSec);
		if ( super.verbose )
			mgwfsDumpFreeMap(&super,"Freelist after sectors free'd:", sampleFreeMap);
	}
	resIdx = 0;
	allocChange = 0;
	while ( allocated < numSectors )
	{
		if ( mgwfsFindFree(&super, &found, numSectors-allocated) )
		{
			allocated += found.result.nblocks;
			allocChange += found.allocChange;
			if ( resIdx < MAX_RESULTS-1 )
			{
				results[resIdx].nblocks = found.result.nblocks;
				results[resIdx].start = found.result.start;
			}
			if ( super.verbose )
			{
				printf("Asked for %d sector%s. (hint=0x%08X) Found 0x%08X-0x%08X (0x%08X [%d] sectors; allocChange=%d, allocated=%d)\n",
					   numSectors,
					   numSectors == 1 ? "" : "s",
					   found.hints[0].start,
					   found.result.start,
					   found.result.start+found.result.nblocks-1,
					   found.result.nblocks,
					   found.result.nblocks,
					   found.allocChange,
					   allocated);
			}
			found.hints[0].start = 0;
			++resIdx;
		}
		else
		{
			printf("Did not find any %ssectors to add\n", allocated ? "more " : "");
			break;
		}
	}
	if ( super.verbose )
	{
		printf("Total allocated: %d, allocChange=%d\n", allocated, allocChange);
		mgwfsDumpFreeMap(&super,"Freelist After:", sampleFreeMap);
		mgwfsDumpFreeMap(&super,"Memory Allocated:", results);
	}
	return 0;
}

#endif	/* STANDALONE_FREELIST */
