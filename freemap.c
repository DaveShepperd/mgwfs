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
#include "mgwfs.h"

typedef uint32_t sector_t;

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

/**
* mgwfsFindFree - allocate sectors
* 
* @author dave (5/17/26)
* 
* At entry:
* @param ourSuper   - pointer to MgwfsSuper_t
* @param stuff      - pointer to MgwfsFoundFreeMap_t
* * result          - ignored
* * hint            - optional RP to possibly extend
* * minSector       - minimum sector to look for
* * allocChange     - ignored
* * dirty           - ignored
* @param numSectors - number of sectors to allocate
* @param flags      - bit mask of 0 or more of
* *                   FREEM_FLAG_xxx
*
* On Exit:
* @return 0 on error, 1 on success
* stuff contents set to:
* * result        - newly formed retrieval pointer
* * hint          - unchanged
* * minSector     - unchanged
*/
int mgwfsFindFree(MgwfsSuper_t *ourSuper, MgwfsFoundFreeMap_t *stuff, int numSectors, uint32_t flags)
{
	if ( stuff )
	{
		int ii, leastDiff, leastDiffIdx;
		FsysRetPtr *src, *src1;
		uint32_t minSector;
		FreeMap_t *freeMapPtr = &ourSuper->freeMap;
		
		/* assume nothing to report */
		stuff->result.nblocks = 0;
		stuff->result.start = 0;
		stuff->actual.nblocks = 0;
		stuff->actual.start = 0;
		minSector = stuff->minSector;
		if ( stuff->hint.nblocks && stuff->hint.start )
		{
			uint32_t contigiousSector = stuff->hint.start + stuff->hint.nblocks;
			if ( (ourSuper->verbose & VERBOSE_FREE) )
			{
				fprintf(ourSuper->logFile,"mgwfsFindFree(): Looking for 0x%X (%d) sector%s. Hint=0x%08X/0x%X. Contigious sector=0x%08X\n",
						numSectors, numSectors, numSectors == 1 ? "" : "s", stuff->hint.start, stuff->hint.nblocks, contigiousSector);
			}
			src = FREEMAP_RP_PTR(freeMapPtr);
			for ( ii = 0; ii < freeMapPtr->freeMapEntriesUsed; ++ii, ++src )
			{
				if ( !src->start || !src->nblocks )
					break;
				if ( src->start == contigiousSector )
				{
					int num = numSectors;
					if ( num > src->nblocks )
						num = src->nblocks;	/* limit the max sectors to add */
					/* we found a connecting section */
					/* we can just extend the provided hinted retrieval */
					stuff->actual.start = src->start;
					stuff->actual.nblocks = num;
					stuff->result.start = stuff->hint.start;
					stuff->result.nblocks = stuff->hint.nblocks + num;
					src->start += num;
					if ( (src->nblocks -= num) <= 0 )
					{
						/* remove the existing section completely */
						memmove(src, src + 1, (freeMapPtr->freeMapEntriesUsed - ii) * sizeof(FsysRetPtr));
						memset(FREEMAP_RP_PTR(freeMapPtr) + freeMapPtr->freeMapEntriesUsed - 1, 0, sizeof(FsysRetPtr));
						--freeMapPtr->freeMapEntriesUsed;
					}
					if ( (flags&FREEM_FLAG_MARK_DIRTY) )
						addToDirty(ourSuper, FSYS_INDEX_FREE);
					return 1;   /* something changed */
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
		src = FREEMAP_RP_PTR(freeMapPtr);
		for ( ii = 0; ii < freeMapPtr->freeMapEntriesUsed; ++ii, ++src )
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
				stuff->actual.start = src->start;
				stuff->actual.nblocks = numSectors;
#if FREEM_FLAG_TRY_ONLY
				if ( !(flags&FREEM_FLAG_TRY_ONLY) )
				{
					/* remove the existing section completely */
					memmove(src, src + 1, (freeMapPtr->freeMapEntriesUsed - ii) * sizeof(FsysRetPtr));
					memset(FREEMAP_RP_PTR(freeMapPtr) + freeMapPtr->freeMapEntriesUsed - 1, 0, sizeof(FsysRetPtr));
					--freeMapPtr->freeMapEntriesUsed;
					if ( (flags&FREEM_FLAG_MARK_DIRTY) )
						addToDirty(ourSuper, FSYS_INDEX_FREE);
				}
#endif
				return 1;   /* something changed */
			}
		}
		src = FREEMAP_RP_PTR(freeMapPtr);
		for ( ii = 0; ii < freeMapPtr->freeMapEntriesUsed; ++ii, ++src )
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
					stuff->result.start = minSector;
					stuff->result.nblocks = numSectors;
					stuff->actual = stuff->result;
					if ( minSector == src->start )
					{
#if FREEM_FLAG_TRY_ONLY
						if ( !(flags&FREEM_FLAG_TRY_ONLY) )
#endif
						{
							/* we can just clip off the new sectors from the current section */
							src->start += numSectors;
							src->nblocks -= numSectors;
						}
					}
					else
					{
						if ( minSector + numSectors != src->start + src->nblocks )
						{
							if ( freeMapPtr->freeMapEntriesUsed >= freeMapPtr->freeMapEntriesAvail )
							{
								/* Technically we could just expand the map file. But for ease here, we just say, sorry, no room */
								return 0;
							}
#if FREEM_FLAG_TRY_ONLY
							if ( !(flags&FREEM_FLAG_TRY_ONLY) )
#endif
							{
								/* we have to insert an entry */
								src1 = src + 1;
								/* make room for one more */
								memmove(src1, src, (freeMapPtr->freeMapEntriesUsed - ii - 1) * sizeof(FsysRetPtr));
								/* Leave start as is but reduce size of area in front */
								src1->start = minSector+numSectors;
								src1->nblocks = (src->start + src->nblocks) - (minSector+numSectors);
								src->nblocks = minSector-src->start;
								++freeMapPtr->freeMapEntriesUsed;
							}
//							++stuff->allocChange;
						}
						else
						{
#if FREEM_FLAG_TRY_ONLY
							if ( !(flags&FREEM_FLAG_TRY_ONLY) )
#endif
							{
								src->nblocks -= numSectors;
							}
						}
					}
					if ( (flags&FREEM_FLAG_MARK_DIRTY) )
						addToDirty(ourSuper, FSYS_INDEX_FREE);
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
					stuff->actual = stuff->result;
#if FREEM_FLAG_TRY_ONLY
					if ( !(flags&FREEM_FLAG_TRY_ONLY) )
#endif
					{
						src->start += numSectors;
						src->nblocks -= numSectors;
						if ( (flags&FREEM_FLAG_MARK_DIRTY) )
							addToDirty(ourSuper, FSYS_INDEX_FREE);
						if ( (ourSuper->verbose & VERBOSE_FREE) )
							fprintf(ourSuper->logFile, "mgwfs_findfree(): returned 0x%08X-0x%08X (0x%X nblocks).\n",
									stuff->result.start,
									stuff->result.start+stuff->result.nblocks-1,
									stuff->result.nblocks);
					}
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
			return mgwfsFindFree(ourSuper,stuff,numSectors, flags);
		}
		src = FREEMAP_RP_PTR(freeMapPtr);
		leastDiffIdx = 0;
		leastDiff = 0x00FFFFFF;
		for ( ii = 0; ii < freeMapPtr->freeMapEntriesUsed; ++ii, ++src )
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
			src = FREEMAP_RP_PTR(freeMapPtr) + leastDiffIdx;
			stuff->result.start = src->start;
			stuff->result.nblocks = src->nblocks;
			stuff->actual = stuff->result;
			/* remove the found section completely */
			memmove(src, src + 1, (freeMapPtr->freeMapEntriesUsed - leastDiffIdx) * sizeof(FsysRetPtr));
			memset(FREEMAP_RP_PTR(freeMapPtr) + freeMapPtr->freeMapEntriesUsed - 1, 0, sizeof(FsysRetPtr));
			if ( (flags&FREEM_FLAG_MARK_DIRTY) )
				addToDirty(ourSuper, FSYS_INDEX_FREE);
			--freeMapPtr->freeMapEntriesUsed;
			return 1;   /* something changed */
		}
	}
	return 0;
}

#define HANDLE_FREE_OVERLAPS (0)
#define MAX_ENT_TST (16)
#define str(xx) #xx
#define xstr(xx) str(xx)

int mgwfsFreeSectors(MgwfsSuper_t *ourSuper, FsysRetPtr *retp, uint32_t flags)
{
	if ( ourSuper && retp )
	{
		int ii;
		FsysRetPtr *src;
		FsysRetPtr *src1;
		FreeMap_t *freeMapPtr = &ourSuper->freeMap;
		uint32_t endRetSector = retp->start + retp->nblocks;
		char txt[256];

		/* Look through the list to see if there is a connection front or back */
		src = FREEMAP_RP_PTR(freeMapPtr);
		if ( (ourSuper->verbose & VERBOSE_FREE) )
		{
			fprintf(ourSuper->logFile, "mgwsFreeSectors(): Freeing 0x%08X-0x%08X (0x%X) sectors.\n",
					retp->start, retp->start + retp->nblocks - 1, retp->nblocks );
			if ( freeMapPtr->freeMapEntriesUsed <= MAX_ENT_TST )
			{
				snprintf(txt,sizeof(txt),"Dumping before free map having fewer than %d entries. Used=%d", MAX_ENT_TST, freeMapPtr->freeMapEntriesUsed );
				mgwfsDumpFreeMap(ourSuper,txt,FREEMAP_RP_PTR(freeMapPtr));
			}
		}
		for ( ii = 0; src->start && src->nblocks && ii < freeMapPtr->freeMapEntriesUsed; ++ii, ++src )
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
				if ( (flags&FREEM_FLAG_MARK_DIRTY) )
					addToDirty(ourSuper, FSYS_INDEX_FREE);
				freeMapPtr->sectorsUsed -= retp->nblocks;
				freeMapPtr->sectorsFree += retp->nblocks;
				if ( (ourSuper->verbose & VERBOSE_FREE) )
				{
					fprintf(ourSuper->logFile,"mgwsFreeSectors(): Free'd in front of entry %4d of %4d to 0x%08X-0x%08X (0x%X)\n",
							ii,
							freeMapPtr->freeMapEntriesUsed,
							src->start,
							src->start + src->nblocks - 1,
							src->nblocks);
					if ( freeMapPtr->freeMapEntriesUsed <= MAX_ENT_TST )
					{
						snprintf(txt,sizeof(txt),"Dumping after free map having fewer than %d entries. Used=%d", MAX_ENT_TST, freeMapPtr->freeMapEntriesUsed );
						mgwfsDumpFreeMap(ourSuper,txt,FREEMAP_RP_PTR(freeMapPtr));
					}
				}
				return 1;
			}
			if ( retp->start == srcEnd )
			{
				if ( src1->start && src1->start < endRetSector )
				{
					snprintf(txt,sizeof(txt), "mgwsFreeSectors(): BUG 1: Tried to free 0x%08X-0x%08X (%d) which overlaps entry %d of %d: 0x%08X-0x%08X (%d)\n",
						   retp->start,
						   retp->start + retp->nblocks - 1,
						   retp->nblocks,
						   ii + 1,
						   freeMapPtr->freeMapEntriesUsed,
						   src1->start,
						   src1->start + src1->nblocks - 1,
						   src1->nblocks);
					if ( 1 || ourSuper->logFile != stdout )
						fputs(txt,ourSuper->logFile);
					fputs(txt, ourSuper->errFile);
					mgwfsDumpFreeMap(ourSuper,"Free list where BUG noticed",FREEMAP_RP_PTR(freeMapPtr));
					return 0;
				}
				/* We are to free after current */
				src->nblocks += retp->nblocks;
				freeMapPtr->sectorsUsed -= retp->nblocks;
				freeMapPtr->sectorsFree += retp->nblocks;
				if ( src1->start && src1->start == endRetSector )
				{
					int numMove;
					/* The free connected two sections so join them together */
					if ( (ourSuper->verbose & VERBOSE_FREE) )
					{
						fprintf(ourSuper->logFile,"mgwsFreeSectors(): Joined adjacent entries %4d of %4d 0x%08X-0x%08X and %d 0x%08X-0x%08X to 0x%08X-0x%08X\n",
								ii,
								freeMapPtr->freeMapEntriesUsed,
								src->start,
								src->start + src->nblocks - 1,
								ii + 1,
								src1->start,
								src1->start + src1->nblocks - 1,
								src->start, src->start + src->nblocks + src1->nblocks - 1);
					}
					src->nblocks += src1->nblocks;
					numMove = freeMapPtr->freeMapEntriesUsed - ii - 2;
					if ( numMove > 0 )
					{
						/* Shift the entries back one */
						memmove(src1, src1 + 1, numMove * sizeof(FsysRetPtr));
					}
					if ( freeMapPtr->freeMapEntriesUsed > 0 )
					{
						/* Signal we removed one */
						--freeMapPtr->freeMapEntriesUsed;
					}
					src1 = FREEMAP_RP_PTR(freeMapPtr) + freeMapPtr->freeMapEntriesUsed;
					/* The last one gets 0's */
					src1->start = 0;
					src1->nblocks = 0;
				}
				if ( (flags&FREEM_FLAG_MARK_DIRTY) )
					addToDirty(ourSuper, FSYS_INDEX_FREE);
				if ( (ourSuper->verbose & VERBOSE_FREE) )
				{
					fprintf(ourSuper->logFile,"mgwsFreeSectors(): Free'd after entry %4d of %4d to 0x%08X-0x%08X (0x%X)\n",
							ii,
							freeMapPtr->freeMapEntriesUsed,
							src->start,
							src->start + src->nblocks - 1,
							src->nblocks);
					if ( freeMapPtr->freeMapEntriesUsed <= MAX_ENT_TST )
					{
						snprintf(txt,sizeof(txt),"Dumping after free map having fewer than %d entries. Used=%d", MAX_ENT_TST, freeMapPtr->freeMapEntriesUsed );
						mgwfsDumpFreeMap(ourSuper,txt,FREEMAP_RP_PTR(freeMapPtr));
					}
				}
				return 1;
			}
			if ( retp->start > srcEnd )
				continue;               /* this should be the normal condition */
			/* Overlap conditions should be recorded as a bug instead of being handled */
			snprintf(txt,sizeof(txt),"mgwsFreeSectors(): BUG 2: Tried to free 0x%08X-0x%08X (0x%X) which overlaps entry %4d of %4d: 0x%08X-0x%08X (0x%X)\n",
					 retp->start,
					 retp->start + retp->nblocks - 1,
					 retp->nblocks,
					 ii,
					 freeMapPtr->freeMapEntriesUsed,
					 src->start,
					 src->start + src->nblocks - 1,
					 src->nblocks);
			if ( 1 || ourSuper->logFile != stdout )
				fputs(txt,ourSuper->logFile);
			fputs(txt,ourSuper->errFile);
#if !HANDLE_FREE_OVERLAPS
			return 0;
#else
			/* Write this code someday */
#endif
		}
		/* Did not find anything so we need to insert a new entry at entry ii */
		if ( ii >= freeMapPtr->freeMapEntriesAvail )
		{
			fprintf(ourSuper->errFile,"mgwsFreeSectors(): No room to add new free entry. ii=%d, freeMapEntriesAvail=%d\n",
					ii, freeMapPtr->freeMapEntriesAvail);
			return 0;
		}
		src = FREEMAP_RP_PTR(freeMapPtr) + ii;
		/* Shift everybody up one to make room */
		memmove(src + 1, src, (freeMapPtr->freeMapEntriesUsed - ii) * sizeof(FsysRetPtr));
		src->start = retp->start;
		src->nblocks = retp->nblocks;
		if ( (flags&FREEM_FLAG_MARK_DIRTY) )
			addToDirty(ourSuper, FSYS_INDEX_FREE);
		++freeMapPtr->freeMapEntriesUsed;
		freeMapPtr->sectorsUsed -= retp->nblocks;
		freeMapPtr->sectorsFree += retp->nblocks;
		if ( (ourSuper->verbose & VERBOSE_FREE) )
		{
			fprintf(ourSuper->logFile,"mgwsFreeSectors(): Added new entry at %4d of %4d. 0x%08X-0x%08X (0x%X)\n",
					ii,
					freeMapPtr->freeMapEntriesUsed,
					src->start,
					src->start + src->nblocks - 1,
					src->nblocks);
			if ( freeMapPtr->freeMapEntriesUsed <= MAX_ENT_TST )
			{
				snprintf(txt,sizeof(txt),"Dumping after free map having fewer than %d entries. Used=%d", MAX_ENT_TST, freeMapPtr->freeMapEntriesUsed );
				mgwfsDumpFreeMap(ourSuper,txt,FREEMAP_RP_PTR(freeMapPtr));
			}
		}
		return 1;
	}
	fprintf(ourSuper->errFile,"mgwsFreeSectors(): Too few parameters provided. ourSuper=%p, retp=%p\n",
			(void *)ourSuper, (void *)retp);
	return 0;
}

#if STANDALONE_FREEMAP

void addToDirty(MgwfsSuper_t *super, int idx)
{
}

#define MAXHB (0x3000+1024*16)

static const FsysRetPtr SampleFreeMapData[] =
{
	{ 0x1000, 10 },
	{ 0x1020, 20 },
	{ 0x1100, 100 },
	{ 0x1300, 1000 },
	{ 0x2400, 1 },
	{ 0x2500, 5 },
	{ 0x3000, 1024*16 },
	{ 0, 0 },
	{ 0, 0 }
};

static uint8_t sampleBuffer[sizeof(SampleFreeMapData)+16];
static FreeMap_t SampleFreeMap;

static int help_em(const char *title)
{
	printf("%s [-v][-c count][-C num][-m min][-s sector][-r sector[,num]] numSectors\n"
		   "Where:\n"
		   "-c count        = specify the count of alt sections to get (1 to 3)\n"
		   "-C num          = specify initial size of free list (default=7)\n"
		   "-m min          = specify the minimum sector to look for\n"
		   "-r sector[,num[,sector,num][,sector[,num]...]] = specify a list of RP's to return\n"
		   "-s sector[,num] = specify a hint RP\n"
		   "-v              = set verbose mode\n"
		   , title);
	return 1;
}

#define MAX_RESULTS (10*FSYS_MAX_ALTS)
#define MAX_NUM_RET_RPS (4)

int main(int argc, char **argv)
{
	int ii, opt, alts=1, resIdx, done=0, allocChange;
	int minSector, numRetRps, numSectors, allocated, numInit=n_elts(SampleFreeMapData)-2;
	char *endp, *nxtRp;
	FsysRetPtr rpReturn[MAX_NUM_RET_RPS];
	MgwfsFoundFreeMap_t found;
	MgwfsSuper_t super;
	FsysRetPtr results[MAX_RESULTS], actuals[MAX_RESULTS], *rp;
	FsysRetPtr sampleFreeMapData[n_elts(SampleFreeMapData)];
	
	memset(&found, 0, sizeof(found));
	memset(&super, 0, sizeof(super));
	memset(rpReturn, 0, sizeof(rpReturn));
	memset(results,0, sizeof(results));
	memset(actuals, 0, sizeof(actuals));
	SampleFreeMap.rwBuff.buff = (uint8_t *)sampleFreeMapData;
	SampleFreeMap.rwBuff.buffOffset = sizeof(sampleFreeMapData)-8;
	SampleFreeMap.rwBuff.buffSize = sizeof(sampleFreeMapData);
	SampleFreeMap.rwBuff.buffUsed = SampleFreeMap.rwBuff.buffOffset;
	SampleFreeMap.freeMapEntriesAvail = n_elts(SampleFreeMapData);
	SampleFreeMap.freeMapEntriesUsed = n_elts(SampleFreeMapData)-2;
	memcpy(sampleBuffer,SampleFreeMapData,SampleFreeMap.rwBuff.buffOffset);
	super.logFile = stdout;
	super.errFile = stderr;
	super.maxHb = MAXHB;
	numRetRps = 0;
	minSector = 0;
	while ( (opt = getopt(argc, argv, "c:C:m:r:s:v")) != -1 )
	{
		switch (opt)
		{
		case 'c':
			endp = NULL;
			alts = strtoul(optarg, &endp, 0);
			if ( !endp || *endp || alts < 1 || alts > FSYS_MAX_ALTS )
			{
				fprintf(stderr, "Bad argument for -c: '%s'. Can only be 1 to %d\n", optarg, FSYS_MAX_ALTS);
				return 1;
			}
			break;
			
		case 'C':
			endp = NULL;
			numInit = strtoul(optarg, &endp, 0);
			if ( !endp || *endp || numInit < 1 || numInit > n_elts(SampleFreeMapData)-2 )
			{
				fprintf(stderr, "Bad argument for -C: '%s'. Can only be 1 to %d\n", optarg, n_elts(SampleFreeMapData)-2);
				return 1;
			}
			break;

		case 'm':
			endp = NULL;
			minSector = strtoul(optarg, &endp, 0);
			if ( !endp || *endp || minSector < 0 || minSector >= 0x00FFFFFF )
			{
				fprintf(stderr, "Bad argument for -m: '%s'. Can only be 0 to 0x00FFFFFF\n", optarg);
				return 1;
			}
			break;

		case 'r':
			endp = NULL;
			nxtRp = optarg;
			while ( numRetRps < MAX_NUM_RET_RPS-1 )
			{
				rpReturn[numRetRps].start = strtoul(nxtRp, &endp, 0);
				rpReturn[numRetRps].nblocks = 1;	/* assume 1 block */
				nxtRp = endp + 1;
				if ( endp && *endp == ',' )
				{
					endp = NULL;
					rpReturn[numRetRps].nblocks = strtoul(nxtRp, &endp, 0);
					nxtRp = endp+1;
				}
				if ( !endp || (*endp && *endp != ',') || rpReturn[numRetRps].start < 4 || rpReturn[numRetRps].start + rpReturn[numRetRps].nblocks >= 0x00FFFFFF )
				{
					fprintf(stderr, "Bad argument(s) for -r: '%s'\n", optarg);
					return 1;
				}
				++numRetRps;
				if ( *endp == ',' )
					continue;
				break;
			}
			if ( numRetRps >= MAX_NUM_RET_RPS-1 && endp && *endp )
			{
				fprintf(stderr, "Too many args for -r. Can only have 1 through %d pairs\n", MAX_NUM_RET_RPS-1 );
				return 1;
			}
			break;
		case 's':
			endp = NULL;
			nxtRp = optarg;
			found.hint.start = strtoul(nxtRp, &endp, 0);
			if ( !endp || (*endp && *endp != ',') || found.hint.start < 4 || found.hint.start >= 0x00FFFFFF )
			{
				fprintf(stderr, "Bad argument for -s: '%s'\n", optarg);
				return 1;
			}
			found.hint.nblocks = 1;
			if ( *endp == ',' )
			{
				nxtRp = endp+1;
				endp = NULL;
				found.hint.nblocks = strtoul(nxtRp, &endp, 0);
				if ( !endp || *endp )
				{
					fprintf(stderr, "Bad argument for -s: '%s'\n", optarg);
					return 1;
				}
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
	memset(sampleFreeMapData,0,sizeof(sampleFreeMapData));
	memcpy(sampleFreeMapData, SampleFreeMapData, numInit*sizeof(FsysRetPtr));
	super.freeMap = SampleFreeMap;
	rp = sampleFreeMapData;
	while ( rp->nblocks )
		++rp;
	allocated = 0;
	for (ii=0; ii < numRetRps; ++ii )
	{
		mgwfsFreeSectors(&super, rpReturn+ii, 0);
		if ( super.verbose )
			mgwfsDumpFreeMap(&super,"Freelist after sectors free'd:", sampleFreeMapData);
	}
	mgwfsDumpFreeMap(&super,"Freelist Before:", sampleFreeMapData);
	resIdx = 0;
	allocChange = 0;
	while ( !done && allocated < numSectors )
	{
		int altIdx;
#if FREEM_FLAG_TRY_ONLY && FREEM_FLAG_NO_RESCAN
		int jj;
		FsysRetPtr tmpRPs[FSYS_MAX_ALTS];
		for (altIdx=0; altIdx < alts; ++altIdx)
		{
			if ( !minSector )
				found.minSector = FSYS_COPY_ALG(altIdx, super.maxHb);
			else
				found.minSector = minSector;
			if ( !mgwfsFindFree(&super, &found, numSectors - allocated, FREEM_FLAG_TRY_ONLY | FREEM_FLAG_NO_RESCAN) )
			{
				printf("mgwfsFindFree(TRYONLY) failed to find %d sectors to add\n", numSectors-allocated);
				done = 1;
				break;
			}
			tmpRPs[altIdx] = found.result;
			for (jj=0; jj < altIdx; ++jj)
			{
				if ( tmpRPs[altIdx].start == tmpRPs[jj].start )
				{
					printf("mgwfsFindFree(TRYONLY) would have allocated duplicate sectors 0x%08X-%08X (0x%08X). Allocating nothing.\n",
						   found.result.start,
						   found.result.start+found.result.nblocks-1,
						   found.result.nblocks);
					done = 1;
					break;
				}
			}
			if ( done )
				break;
			printf("Might get to allocate sectors 0x%08X-0x%08X (0x%08X).\n",
				   found.result.start,
				   found.result.start+found.result.nblocks-1,
				   found.result.nblocks);
		}
		if ( done )
			break;
#endif
		for ( altIdx = 0; altIdx < alts; ++altIdx )
		{
			if ( !minSector )
				found.minSector = FSYS_COPY_ALG(altIdx, super.maxHb);
			else
				found.minSector = minSector;
			if ( !mgwfsFindFree(&super, &found, numSectors-allocated, 0 /* | FREEM_FLAG_NO_RESCAN */) )
			{
				printf("mgwfsFindFree(!TRYONLY): Did not find any %ssectors to add\n", allocated ? "more " : "");
				done = 1;
				break;
			}
			if ( resIdx < MAX_RESULTS-1 )
			{
				results[resIdx].nblocks = found.result.nblocks;
				results[resIdx].start = found.result.start;
				actuals[resIdx].nblocks = found.actual.nblocks;
				actuals[resIdx].start = found.actual.start;
			}
			if ( super.verbose )
			{
				printf("Asked for %d sector%s. (hint=0x%08X/0x08%X, min=0x%08X) Found 0x%08X-0x%08X (0x%08X [%d] sectors; allocated=%d)\n",
					   numSectors,
					   numSectors == 1 ? "" : "s",
					   found.hint.start,
					   found.hint.nblocks,
					   found.minSector,
					   found.result.start,
					   found.result.start+found.result.nblocks-1,
					   found.result.nblocks,
					   found.result.nblocks,
					   allocated);
			}
			found.hint.start = 0;
			found.hint.nblocks = 0;
			++resIdx;
			allocChange += 1;
		}
		allocated += found.result.nblocks;
	}
	printf("Total allocated: %d, allocChange=%d\n", allocated, allocChange);
	mgwfsDumpFreeMap(&super,"Freelist After:", sampleFreeMapData);
	mgwfsDumpFreeMap(&super,"Space Allocated results:", results);
	mgwfsDumpFreeMap(&super,"Space Allocated actuals:", actuals);
	return 0;
}

#endif	/* STANDALONE_FREELIST */
