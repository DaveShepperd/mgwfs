/* NOTE: This file is here just for reference.
 * It's contents has been moved into freelist.c and
 * the function has been enabled by setting STANDALONE_FREELIST.
 */
#if 0
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

typedef struct
{
	uint32_t sector;
	int numSectors;
} FreeList_t;

static FreeList_t sampleFreeList[] =
{
#if 0
	{ 0x00001A2E, 10 },
	{ 0x00001C07, 10 },
	{ 0x0002BAE6, 10 },
	{ 0x0002BB54, 60 },
	{ 0x0002BB9A, 10 },
	{ 0x0002BC58, 38086 },
	{ 0x0015E850, 7326 },
	{ 0x0016C872, 61442 },
	{ 0x0018CDAD, 414723 },
	{ 0x00235BC2, 138241 },
	{ 0x002EA021, 2 },
	{ 0x002EA058, 79 },
	{ 0x002FB992, 8 },
	{ 0x00300549, 2092 },
	{ 0x00300D78, 5 },
	{ 0x00300DE4, 1 },
	{ 0x00300DE6, 612 },
	{ 0x0030104B, 2138 },
	{ 0x00306480, 1 },
	{ 0x003151B5, 1453 },
	{ 0x00321E38, 2 },
	{ 0x00321FF3, 3 },
	{ 0x00322208, 1 },
	{ 0x003A9A67, 184322 },
	{ 0x003ED29E, 79 },
	{ 0x00405DD8, 1024 },
	{ 0x0040623E, 3 },
	{ 0x004062A7, 52 },
	{ 0x00406341, 969 },
	{ 0x00406770, 5778 },
	{ 0x00408979, 22439 },
	{ 0x0053A848, 46 },
	{ 0x0055B0AE, 4 },
	{ 0x0055B0B4, 2 },
	{ 0x0055B0BA, 3 },
	{ 0x0055B0BE, 10 },
	{ 0x0055B194, 60 },
	{ 0x0055B236, 3852 },
	{ 0x0055C1A8, 61200 },
	{ 0x00579FC8, 545593 },
	{ 0x005FFF49, 2092 },
	{ 0x00600778, 5 },
	{ 0x006007E4, 1 },
	{ 0x006007E6, 612 },
	{ 0x00600A4B, 2138 },
	{ 0x00605E80, 1 },
	{ 0x00614BB5, 1453 },
	{ 0x00621838, 2 },
	{ 0x006219F3, 3 },
	{ 0x00621C08, 1 },
	{ 0x00622467, 2 },
	{ 0x0062249E, 79 },
	{ 0x006225D8, 8 },
	{ 0x006477DA, 23 },
	{ 0x00651B1A, 2 },
	{ 0x00651B1D, 1 },
	{ 0x00651B87, 5 },
	{ 0x00651BBF, 321 },
	{ 0x00651D33, 4 },
	{ 0x00651D6A, 1 },
	{ 0x00651D9E, 10 },
	{ 0x00651DDB, 323 },
	{ 0x00651F51, 172105 },
	{ 0x006A549A, 207360 },
	{ 0x0070A89A, 2049019 }
#else
	{ 0x1000,10 },
	{ 0x1020,20 },
	{ 0x1100,100 },
	{ 0x1300, 1000 },
	{ 0x2400, 1 },
	{ 0x2500, 5 },
	{ 0, 0 },
	{ 0, 0 }
#endif
};

static void dumpFreeList( const char *title, const FreeList_t *list )
{
	int ii=0;
	if ( title )
		printf("%s\n", title);
	while ( list->sector )
	{
		printf("%3d: 0x%08X-0x%08X (0x%X)\n",
			   ii, list->sector, list->sector + list->numSectors - 1, list->numSectors);
		++ii;
		++list;
	}
}

typedef struct
{
	FreeList_t *currList;	/* Pointer to current list of entries */
	int currListAlloc;		/* maximum number of entries in list */
	int updatedEntryIndex;	/* index of entry updated */
	int addedEntryIndex;	/* index of entry added */
	int dirty;				/* boolean indicating area has been modified */
	FreeList_t result;		/* newly formed selection */
	FreeList_t hint;		/* hint of what to connect to if possible */
	int verbose;
} FoundFreeList_t;

static int findFree(FoundFreeList_t *stuff, int numSectors )
{
	if ( stuff )
	{
		int ii, leastDiff, leastDiffIdx;
		FreeList_t *src;
		
		/* assume nothing to report */
		stuff->result.numSectors = 0;
		stuff->result.sector = 0;
		if ( stuff->hint.sector && stuff->hint.sector )
		{
			uint32_t contigiousSector = stuff->hint.sector + stuff->hint.numSectors;
			/* A hint was provided. So see if there is a connecting section */
			if ( stuff->verbose )
			{
				printf("Looking for 0x%X sector%s. Hint=0x%08X/0x%X. Contigious sector=0x%08X\n",
					   numSectors, numSectors == 1 ? "":"s", stuff->hint.sector, stuff->hint.numSectors, contigiousSector );
			}
			src = stuff->currList;
			for (ii=0; ii < stuff->currListAlloc; ++ii, ++src)
			{
				if ( !src->sector || !src->numSectors )
					break;
				if ( src->sector == contigiousSector  )
				{
					int num = numSectors;
					if ( num > src->numSectors )
						num = src->numSectors;
					/* we found a connecting section */
					/* we can just clip off sectors from the current section */
					stuff->result.sector = src->sector;
					stuff->result.numSectors = num;
					src->sector += num;
					if ( !(src->numSectors -= num) )
					{
						/* remove the existing section completely */
						memmove(src, src + 1, (stuff->currListAlloc-ii)*sizeof(FreeList_t));
						memset(stuff->currList+stuff->currListAlloc-1,0,sizeof(FreeList_t));
					}
					stuff->updatedEntryIndex = ii;
					stuff->dirty = 1;
					return 1;	/* something changed */
				}
			}
		}
		/* Did not find anything we can add contigious to an existing section */
		if ( stuff->verbose )
		{
			printf("Looking for 0x%X sector%s from anywhere.\n",
				   numSectors, numSectors == 1 ? "":"s");
		}
		src = stuff->currList;
		for (ii=0; ii < stuff->currListAlloc; ++ii, ++src)
		{
			if ( !src->sector )
				break;
			if ( src->numSectors == numSectors )
			{
				/* we found a section with the requested number of sectors exactly */
				/* we can just clip off all of the new sectors from the current section */
				stuff->result.sector = src->sector;
				stuff->result.numSectors = numSectors;
				/* remove the existing section completely */
				memmove(src, src + 1, (stuff->currListAlloc-ii)*sizeof(FreeList_t));
				memset(stuff->currList+stuff->currListAlloc-1,0,sizeof(FreeList_t));
				stuff->updatedEntryIndex = ii;
				stuff->dirty = 1;
				return 1;	/* something changed */
			}
		}
		src = stuff->currList;
		for (ii=0; ii < stuff->currListAlloc; ++ii, ++src)
		{
			if ( !src->sector )
				break;
			if ( src->numSectors > numSectors )
			{
				/* we found a section with at least the requested number of sectors */
				/* we can just clip off the new sectors from the current section */
				stuff->result.sector = src->sector;
				stuff->result.numSectors = numSectors;
				src->sector += numSectors;
				src->numSectors -= numSectors;
				stuff->updatedEntryIndex = ii;
				stuff->dirty = 1;
				return 1;	/* something changed */
			}
		}
		src = stuff->currList;
		leastDiffIdx = 0;
		leastDiff = 0x00FFFFFF;
		for (ii=0; ii < stuff->currListAlloc; ++ii, ++src)
		{
			if ( !src->sector )
				break;
			if ( numSectors - src->numSectors < leastDiff )
			{
				leastDiff = numSectors - src->numSectors;
				leastDiffIdx = ii;
			}
		}
		if ( stuff->verbose )
			printf("Found leastDiff=0x%X, leastDiffIdx=%d\n", leastDiff, leastDiffIdx );
		if ( leastDiff < 0x00FFFFFF )
		{
			src = stuff->currList+leastDiffIdx;
			stuff->result.sector = src->sector;
			stuff->result.numSectors = src->numSectors;
			/* remove the found section completely */
			memmove(src, src + 1, (stuff->currListAlloc-leastDiffIdx)*sizeof(FreeList_t));
			memset(stuff->currList+stuff->currListAlloc-1,0,sizeof(FreeList_t));
			stuff->updatedEntryIndex = leastDiffIdx;
			stuff->dirty = 1;
			return 1;	/* something changed */
		}
	}
	return 0;
}

#define HANDLE_FREE_OVERLAPS (0)

static int freeSectors(FoundFreeList_t *stuff, FreeList_t *retp)
{
	if ( stuff && retp )
	{
		int ii;
		FreeList_t *src;
		FreeList_t *src1;
		uint32_t endRetSector = retp->sector + retp->numSectors;

		/* Look through the list to see if there is a connection front or back */
		src = stuff->currList;
		if ( stuff->verbose )
			printf("Freeing 0x%08X-0x%08X (0x%X) sectors\n", retp->sector, retp->sector + retp->numSectors - 1, retp->numSectors);
		for ( ii = 0; src->sector && src->numSectors && ii < stuff->currListAlloc; ++ii, ++src )
		{
			uint32_t srcEnd;

			/* If the last sector to free is below the current, we're done looking. */
			if ( endRetSector < src->sector )
				break;
			srcEnd = src->sector + src->numSectors;
			/* Point to next one on the list */
			src1 = src+1;
#if !HANDLE_FREE_OVERLAPS
			if ( endRetSector == src->sector )
			{
				/* We are to free ahead of current */
				src->sector = retp->sector;
				src->numSectors += retp->numSectors;
				stuff->dirty = 1;
				stuff->updatedEntryIndex = ii;
				if ( stuff->verbose )
					printf("Free'd in front of entry %d to 0x%08X-0x%08X (0x%X)\n", ii, src->sector, src->sector + src->numSectors-1, src->numSectors);
				return 1;
			}
			if ( retp->sector == srcEnd )
			{
				if ( src1->sector && src1->sector < endRetSector )
				{
					printf("BUG: Tried to free 0x%08X-0x%08X (%d) which overlaps entry %d: 0x%08X-0x%08X (%d)\n",
						   retp->sector, retp->sector + retp->numSectors-1, retp->numSectors,
						   ii+1, src1->sector, src1->sector + src1->numSectors - 1, src1->numSectors);
					return 0;
				}
				/* We are to free behind current */
				src->numSectors += retp->numSectors;
				if ( src1->sector && src1->sector == endRetSector )
				{
					int numMove;
					/* The free connected two sections so join them together */
					if ( stuff->verbose )
						printf("Joined adjacent entries %d 0x%08X-0x%08X and %d 0x%08X-0x%08X to 0x%08X-0x%08X\n",
							   ii, src->sector, src->sector + src->numSectors-1,
							   ii + 1, src1->sector, src1->sector + src1->numSectors-1,
							   src->sector, src->sector + src->numSectors + src1->numSectors-1);
					src->numSectors += src1->numSectors;
					numMove = stuff->currListAlloc-ii-2;
					/* Delete an entry as long as it's not the last */
					if ( numMove > 0 )
						memmove(src1, src1 + 1, numMove * sizeof(FreeList_t));
					src1 = stuff->currList + stuff->currListAlloc-1;
					/* The last one gets 0's */
					src1->sector = 0;
					src1->numSectors = 0;
				}
				stuff->dirty = 1;
				stuff->updatedEntryIndex = ii;
				if ( stuff->verbose )
					printf("Free'd behind entry %d to 0x%08X-0x%08X (0x%X)\n", ii, src->sector, src->sector + src->numSectors-1, src->numSectors);
				return 1;
			}
			if ( retp->sector > srcEnd )
				continue;				/* this should be the normal condition */
			/* Overlap conditions should be recorded as a bug instead of handled */
			printf("BUG: Tried to free 0x%08X-0x%08X (0x%X) which overlaps entry %d: 0x%08X-0x%08X (0x%X)\n",
				   retp->sector, retp->sector + retp->numSectors-1, retp->numSectors,
				   ii, src->sector, src->sector + src->numSectors - 1, src->numSectors);
			return 0;
#else
			/* Write this code someday */
#endif
		}
		/* Did not find anything so we need to insert a new entry at entry ii */
		if ( ii >= stuff->currListAlloc )
		{
			printf("No room to add new free entry. ii=%d, currListAlloc=%d\n",
				   ii, stuff->currListAlloc);
			return 0;
		}
		src = stuff->currList + ii;
		memmove(src+1,src,(stuff->currListAlloc-ii)*sizeof(FreeList_t));
		src->sector = retp->sector;
		src->numSectors = retp->numSectors;
		stuff->dirty = 1;
		stuff->addedEntryIndex = ii;
		if ( stuff->verbose )
			printf("Added new last %d of 0x%08X-0x%08X (0x%X)\n", ii, src->sector, src->sector + src->numSectors-1, src->numSectors);
		return 1;
	}
	if ( stuff && stuff->verbose )
		printf("Not enough parameters provided. stuff=%p, retp=%p\n", (void *)stuff, (void *)retp);
	return 0;
}

static int help_em(const char *title)
{
	printf("%s [-v][-s sector][-r sector[,num]] numSectors\n"
		   "Where:\n"
		   "-r sector[,num] = specify sector and optional number of sectors to return to freelist\n"
		   "-s sector       = specify a sector hint\n"
		   "-v              = set verbose mode\n"
		   ,title);
	return 1;
}

int main(int argc, char **argv)
{
	int opt;
	int numSectors, allocated;
	char *endp;
	FreeList_t retSec;
	FoundFreeList_t found;
	
	memset(&found,0,sizeof(found));
	retSec.numSectors = 0;
	retSec.sector = 0;
	while ( (opt=getopt(argc,argv,"r:s:v")) != -1 )
	{
		switch (opt)
		{
		case 'r':
			endp = NULL;
			retSec.sector = strtol(optarg, &endp, 0);
			retSec.numSectors = 1;
			if ( endp && *endp == ',' )
			{
				char *numSec = endp+1;
				endp = NULL;
				retSec.numSectors = strtol(numSec,&endp,0);
			}
			if ( !endp || *endp || retSec.sector < 4 || retSec.sector + retSec.numSectors >= 0x00FFFFFF )
			{
				fprintf(stderr,"Bad argument for -r: '%s'\n", optarg);
				return 1;
			}
			break;
		case 's':
			endp = NULL;
			found.hint.sector = strtol(optarg, &endp, 0);
			if ( !endp || *endp || found.hint.sector < 4 || found.hint.sector >= 0x00FFFFFF )
			{
				fprintf(stderr,"Bad argument for -s: '%s'\n", optarg);
				return 1;
			}
			break;
		case 'v':
			found.verbose = 1;
			break;
		default:
			fprintf(stderr,"Undefined command line arg: '%c'(%d)\n", isprint(opt)?opt:'.', opt);
			return help_em(argv[0]);
		}
	}
/*	printf("argc=%d, optind=%d\n", argc, optind); */
	if ( argc-optind < 1 )
		return help_em(argv[0]);
	endp = NULL;
	numSectors = strtol(argv[optind],&endp,0);
	if ( !endp || *endp || numSectors < 1 || numSectors >= 0x00FFFFFF )
	{
		fprintf(stderr,"Bad argument for -n: '%s'\n", argv[optind]);
		return 1;
	}
	found.currListAlloc = sizeof(sampleFreeList)/sizeof(FreeList_t);
	found.currList = sampleFreeList;
	if ( found.verbose )
		dumpFreeList("Before:",sampleFreeList);
	allocated = 0;
	if ( retSec.sector )
	{
		freeSectors(&found,&retSec);
		if ( found.verbose )
			dumpFreeList("After Free:",sampleFreeList);
	}
	while ( allocated < numSectors )
	{
		if ( findFree(&found, numSectors) )
		{
			allocated += found.result.numSectors;
			if ( found.verbose )
			{
				printf("Asked for %d sector%s. (hint=0x%08X) Found %d at 0x%08X (updIndex=%d, addedIdx=%d, allocated=%d)\n",
					   numSectors,
					   numSectors==1 ? "":"s",
					   found.hint.sector,
					   found.result.numSectors,
					   found.result.sector,
					   found.updatedEntryIndex,
					   found.addedEntryIndex,
					   allocated);
			}
			found.hint.sector = 0;
		}
		else
		{
			printf("Did not find any %ssectors to add\n", allocated ? "more ":"" );
			break;
		}
	}
	if ( found.verbose )
	{
		printf("Total allocated: %d\n", allocated);
		dumpFreeList("After:",sampleFreeList);
	}
	return 0;
}
#endif	/* 0 */
