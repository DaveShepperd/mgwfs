/*
  mgwfsf: Atari/MidwayGamesWest filesystem using libfuse: Filesystem in Userspace

  Copyright (C) 2025  Dave Shepperd <mgwfsf@dshepperd.com>

  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.

 Compile with:
 gcc -Wall -c -g mgwfsf.c -I/usr/local/include/fuse3
 gcc -Wall -c -g freemap.c
 gcc -g -o mgwfsf mgwfsf.o freemap.o -lfuse3 -lpthread

*/

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
//#include <getopt.h>

typedef uint32_t sector_t;
#include "mgwfsf.h"

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

static BootSector_t bootSect;
MgwfsSuper_t ourSuper;

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

static Options_t options;

static void displayFileHeader(FILE *outp, FsysHeader *fhp, int retrievalsToo)
{
	FsysRetPtr *rp;
	int alt,ii;
	
	fprintf(outp,  "    id:        0x%X\n"
			       "    size:      %d\n"
				   "    clusters:  %d\n"
				   "    generation:%d\n"
				   "    type:      %d\n"
				   "    flags:     0x%04X\n"
				   "    ctime:     %d\n"
				   "    mtime:     %d\n"
			, fhp->id
			, fhp->size
			, fhp->clusters
			, fhp->generation
			, fhp->type
			, fhp->flags
			, fhp->ctime
			, fhp->mtime
			);
	
	for (alt=0; alt < FSYS_MAX_ALTS; ++alt)
	{
		rp = fhp->pointers[alt];
		for (ii=0; ii < FSYS_MAX_FHPTRS; ++ii, ++rp )
		{
			if ( !rp->start || !rp->nblocks )
				break;
		}
		rp = fhp->pointers[alt];
		fprintf(outp, "    pointers[%d] (%d of %ld):", alt, ii, FSYS_MAX_FHPTRS);
		if ( retrievalsToo )
		{
			for (ii=0; ii < FSYS_MAX_FHPTRS; ++ii, ++rp )
			{
				if ( !rp->start || !rp->nblocks )
					break;
				fprintf(outp, " 0x%X/%d", rp->start, rp->nblocks);
			}
		}
		else
			fprintf(outp, " 0x%X/%d%s", rp->start, rp->nblocks, (rp[1].start && rp[1].nblocks) ? " ...":"");
		fprintf(outp, "\n");
		if ( !(retrievalsToo&VERBOSE_RETPTRS) )
			break;
	}
	fflush(outp);
}

static void displayHomeBlock(FILE *outp, const FsysHomeBlock *homeBlkp, uint32_t cksum)
{
	fprintf(outp,
		   "    id:        0x%X\n"
		   "    hb_minor:  %d\n"
		   "    hb_major:  %d\n"
		   "    hb_size:   %d\n"
		   "    fh_minor:  %d\n"
		   "    fh_major:  %d\n"
		   "    fh_size:   %d\n"
		   "    efh_minor: %d\n"
		   "    efh_major: %d\n"
		   "    efh_size:  %d\n"
		   "    efh_ptrs:  %d\n"
		   "    rp_minor:  %d\n"
		   "    rp_major:  %d\n"
		   "    rp_size:   %d\n"
		   "    cluster:   %d\n"
		   "    maxalts:   %d\n"
		   ,homeBlkp->id
		   ,homeBlkp->hb_minor
		   ,homeBlkp->hb_major
		   ,homeBlkp->hb_size
		   ,homeBlkp->fh_minor
		   ,homeBlkp->fh_major
		   ,homeBlkp->fh_size
		   ,homeBlkp->efh_minor
		   ,homeBlkp->efh_major
		   ,homeBlkp->efh_size
		   ,homeBlkp->efh_ptrs
		   ,homeBlkp->rp_minor
		   ,homeBlkp->rp_major
		   ,homeBlkp->rp_size
		   ,homeBlkp->cluster
		   ,homeBlkp->maxalts
		   );
	printf(    "    def_extend:%d\n"
			   "    ctime:     %d\n"
			   "    mtime:     %d\n"
			   "    atime:     %d\n"
			   "    btime:     %d\n"
			   "    chksum:    0x%X (computed 0x%X)\n"
			   "    features:  0x%08X\n"
			   "    options:   0x%08X\n"
			   "    index[]:   0x%08X,0x%08X,0x%08X\n"
			   "    boot[]:    0x%08X,0x%08X,0x%08X\n"
			   "    max_lba:   0x%08X\n"
			   "    upd_flag:  %d\n"
			   , homeBlkp->def_extend
			   , homeBlkp->ctime
			   , homeBlkp->mtime
			   , homeBlkp->atime
			   , homeBlkp->btime
			   , homeBlkp->chksum, cksum
			   , homeBlkp->features
			   , homeBlkp->options
			   , homeBlkp->index[0],homeBlkp->index[1],homeBlkp->index[2]
			   , homeBlkp->boot[0],homeBlkp->boot[1],homeBlkp->boot[2]
			   , homeBlkp->max_lba
			   , homeBlkp->upd_flag
			   );
	printf("    boot1[]:   0x%08X, 0x%08X, 0x%08X\n"
		   "    boot2[]:   0x%08X, 0x%08X, 0x%08X\n"
		   "    boot3[]:   0x%08X, 0x%08X, 0x%08X\n"
		   "    journal[]: 0x%08X, 0x%08X, 0x%08X\n"
		   ,homeBlkp->boot1[0], homeBlkp->boot1[1], homeBlkp->boot1[2]
		   ,homeBlkp->boot2[0], homeBlkp->boot2[1], homeBlkp->boot2[2]
		   ,homeBlkp->boot3[0], homeBlkp->boot3[1], homeBlkp->boot3[2]
		   ,homeBlkp->journal[0], homeBlkp->journal[1], homeBlkp->journal[2]
		   );
	fflush(outp);
}

static int getHomeBlock(MgwfsSuper_t *ourSuper, uint32_t *lbas, off64_t maxHb, off64_t sizeInSectors, uint32_t *ckSumP)
{
	FsysHomeBlock *homeBlkp = &ourSuper->homeBlk;
	FsysHomeBlock lclHomes[FSYS_MAX_ALTS], *lclHome=lclHomes;
	int fd, ii, good=0, match=0;
	ssize_t sts;
	off64_t sector;
	int jj;
	uint32_t options;
	uint32_t *csp,cksum;
	
	memset(lclHomes,0,sizeof(lclHomes));
	*ckSumP = 0;
	fd = ourSuper->fd;
	for (ii=0; ii < FSYS_MAX_ALTS; ++ii, ++lclHome, ++lbas)
	{
		sector = *lbas+ourSuper->baseSector;
		if ( (ourSuper->verbose&VERBOSE_HOME) )
			printf("Attempting to read home block at sector 0x%lX\n", sector);
		if ( lseek64(fd,sector*512,SEEK_SET) == (off64_t)-1 )
		{
			fprintf(stderr,"Failed to seek to sector 0x%lX: %s\n", sector, strerror(errno));
			continue;
		}
		sts = read(fd, lclHome, sizeof(FsysHomeBlock));
		if ( sts != sizeof(FsysHomeBlock) )
		{
			fprintf(stderr,"Failed to read %ld byte home block at sector 0x%lX: %s\n", sizeof(FsysHomeBlock), sector, strerror(errno));
			continue;
		}
		cksum = 0;
		csp = (uint32_t *)lclHome;
		for (jj=0; jj < sizeof(FsysHomeBlock)/sizeof(uint32_t); ++jj)
			cksum += *csp++;
		options = lclHome->features & lclHome->options & (FSYS_FEATURES_CMTIME | FSYS_FEATURES_EXTENSION_HEADER | FSYS_FEATURES_ABTIME);
		if (    lclHome->id == FSYS_ID_HOME
			 && lclHome->rp_major == 1
			 && lclHome->rp_minor == 1
			 && !cksum
			 && options == FSYS_FEATURES_CMTIME
			 && lclHome->fh_size == 504
			 && lclHome->maxalts == FSYS_MAX_ALTS
		   )
		{
			good |= 1<<ii;
			*ckSumP = cksum;
		}
		else
		{
			fprintf(stderr,"Home block %d is not what is expected:\n", ii);
			displayHomeBlock(stderr, lclHome, cksum);
			continue;
		}
		if ( !ii )
		{
			match = 1;
		}
		else
		{
			if ( !memcmp(lclHomes+0,lclHome,sizeof(FsysHomeBlock)) )
				match |= 1<<ii;
			else
				fprintf(stderr,"Header %d does not match header 0\n", ii);
		}
	}
	if ( !good )
	{
		memset(homeBlkp,0,sizeof(FsysHomeBlock));
		return 0;
	}
	lclHome = lclHomes;
	if ( !(good&1) )
	{
		++lclHome;
		if ( !(good&2) )
			++lclHome;
	}
	memcpy(homeBlkp,lclHome,sizeof(FsysHomeBlock));
	return good;
}

static int getFileHeader(const char *title, MgwfsSuper_t *ourSuper, uint32_t id, uint32_t lbas[FSYS_MAX_ALTS], FsysHeader *fhp)
{
	FsysHeader lclHdrs[FSYS_MAX_ALTS], *lclFhp=lclHdrs;
	int fd, ii, good=0, match=0;
	ssize_t sts;
	off64_t sector;
	
	memset(lclHdrs,0,sizeof(lclHdrs));
	fd = ourSuper->fd;
	for (ii=0; ii < FSYS_MAX_ALTS; ++ii, ++lclFhp)
	{
		sector = lbas[ii]+ourSuper->baseSector;
		if ( (ourSuper->verbose&VERBOSE_HEADERS) )
			printf("Attempting to read file header for '%s' at sector 0x%lX\n", title, sector);
		if ( lseek64(fd,sector*512,SEEK_SET) == (off64_t)-1 )
		{
			fprintf(stderr,"Failed to seek to sector 0x%lX: %s\n", sector, strerror(errno));
			continue;
		}
		sts = read(fd, lclFhp, sizeof(FsysHeader));
		if ( sts != sizeof(FsysHeader) )
		{
			fprintf(stderr,"Failed to read %ld byte file header at sector 0x%lX: %s\n", sizeof(FsysHeader), sector, strerror(errno));
			continue;
		}
		if ( lclFhp->id != id )
		{
			fprintf(stderr, "Sector at 0x%lX is not a file header:\n", sector);
			displayFileHeader(stderr,lclFhp,0);
			continue;
		}
		good |= 1<<ii;
		if ( !ii )
		{
			match = 1;
		}
		else
		{
			if ( !memcmp(lclHdrs,lclFhp,sizeof(FsysHeader)) )
				match |= 1<<ii;
			else
				fprintf(stderr,"Header %d does not match header 0\n", ii);
		}
	}
	if ( good )
	{
		lclFhp = lclHdrs;
		if ( !(good&1) )
		{
			++lclFhp;
			if ( !(good&2) )
				++lclFhp;
		}
		memcpy(fhp,lclFhp,sizeof(FsysHeader));
	}
	else
	{
		memset(fhp,0,sizeof(FsysHeader));
		return 0;
	}
	return 1;
}

static int readFile(const char *title,  MgwfsSuper_t *ourSuper, uint8_t *dst, int bytes, FsysRetPtr *retPtr)
{
	off64_t sector;
	int fd, ptrIdx=0, retSize=0, blkLimit;
	ssize_t rdSts, limit;
	
	fd = ourSuper->fd;
	while ( retSize < bytes )
	{
		if ( !retPtr->start || !retPtr->nblocks  )
		{
			fprintf(stderr,"Empty retrieval pointer at retIdx %d while reading '%s'\n", ptrIdx, title);
			return -1;
		}
		sector = retPtr->start;
		blkLimit = retPtr->nblocks;
		limit = bytes - retSize;
		if ( blkLimit*BYTES_PER_SECTOR > limit )
			blkLimit = ((blkLimit*BYTES_PER_SECTOR-limit)+BYTES_PER_SECTOR-1)/512;
		if ( (ourSuper->verbose&VERBOSE_READ) )
		{
			printf("Attempting to read %ld bytes for %s. ptrIdx=%d, sector=0x%X, nblocks=%d (limited blocks=%d)\n",
			   limit, title, ptrIdx, retPtr->start, retPtr->nblocks, blkLimit);
		}
		if ( lseek64(fd,(sector+ourSuper->baseSector)*BYTES_PER_SECTOR,SEEK_SET) == (off64_t)-1 )
		{
			fprintf(stderr,"Failed to seek to sector 0x%lX: %s\n", sector, strerror(errno));
			return -1;
		}
		if ( limit > retPtr->nblocks*BYTES_PER_SECTOR )
		{
			limit = retPtr->nblocks*BYTES_PER_SECTOR;
			++retPtr;
			++ptrIdx;
		}
		rdSts = read(fd, dst+retSize, limit);
		if ( rdSts != limit )
		{
			fprintf(stderr,"Failed to read %ld bytes for %s. Instead got %ld: %s\n", limit, title, rdSts, strerror(errno));
			return -1;
		}
		retSize += rdSts;
	}
	return retSize;
}

static void dumpIndex(uint32_t *indexBase, int bytes)
{
	int ii=0;
	uint32_t *index = indexBase;
	const char *Titles[] = 
	{
		"index.sys",
		"freemap.sys",
		"rootdir.sys",
		"journal.sys"
	};
	printf("Contents of index.sys:\n");
	while ( index < indexBase+(bytes+sizeof(uint32_t)-1)/sizeof(uint32_t) )
	{
		printf("    %5d: 0x%08X 0x%08X 0x%08X", ii, index[0], index[1], index[2]);
		if ( ii < 4 )
			printf(" (%s)", Titles[ii]);
		printf("\n");
		++ii;
		index += 3;
	}
}

static void dumpFreemap(const char *title, FsysRetPtr *rpBase, int entries )
{
	FsysRetPtr *rp = rpBase;
	int ii=0;
	int32_t freeSize=0;
	
	if ( title )
		printf("%s\n", title);
	while ( rp < rpBase+entries )
	{
		freeSize += rp->nblocks;
		printf("    %5d: 0x%08X-0x%08X (%d)\n", ii, rp->start, rp->nblocks ? rp->start+rp->nblocks-1:rp->start, rp->nblocks);
		if ( !rp->start || !rp->nblocks )
			break;
		++ii;
		++rp;
	}
	printf("    Total size: %d sectors\n", freeSize);
}

void dumpDir(uint8_t *dirBase, int bytes, MgwfsSuper_t *ourSuper, uint32_t *indexSys )
{
	uint8_t *dir = dirBase;
	int fd, ii=0;
	FsysHeader hdr;
	
	printf("Contents of rootdir.sys:\n");
	fd = ourSuper->fd;
	while ( dir < dirBase+bytes )
	{
		int txtLen;
		uint8_t gen;
		uint32_t fid;

		fid = (dir[2]<<16)|(dir[1]<<8)|dir[0];
		if ( fid == 0 )
			break;
		dir += 3;
		gen = *dir++;
		txtLen = *dir++;
		if ( !txtLen )
			txtLen = 256;
		printf("    %5d: 0x%08X 0x%02X %3d %s\n", ii, fid, gen, txtLen, dir );
		if ( fd > 0 && indexSys )
		{
			if ( getFileHeader((char *)dir, ourSuper, FSYS_ID_HEADER, indexSys + fid * FSYS_MAX_ALTS, &hdr) )
				displayFileHeader(ourSuper->logFile,&hdr,1|(ourSuper->verbose&VERBOSE_RETPTRS));
		}
		++ii;
		dir += txtLen;
	}
}

#if 0
int iterateDir(int nest, uint8_t *dirBase, int bytes, MgwfsSuper_t *ourSuper, uint32_t *indexSys)
{
	uint8_t *dir = dirBase;
	int fd, ii=0;
	FsysHeader hdr;
	int ret=0;
	
	fd = ourSuper->fd;
	if ( nest > 16 )
	{
		fprintf(stderr,"Directory nest at 16 is too deep\n");
		return 1;
	}
	while ( dir < dirBase + bytes )
	{
		int txtLen;
		uint32_t fid;
		uint8_t gen;
		
		fid = (dir[2]<<16)|(dir[1]<<8)|dir[0];
		if ( fid == 0 )
			break;
		dir += 3;
		gen = *dir++;
		txtLen = *dir++;
		if ( !txtLen )
			txtLen = 256;
		if ( dir[0] != '.' )	/* skip all files starting with a dot */
		{
			if ( getFileHeader((char *)dir, ourSuper, FSYS_ID_HEADER, indexSys + fid * FSYS_MAX_ALTS, &hdr) && hdr.generation  == gen )
			{
				if ( hdr.type == FSYS_TYPE_DIR  )
				{
					uint8_t *fileBuff;
					printf("%*s%5d: 0x%08X DIR 0x%08X/0x%08X 0x%02X %s\n", nest*4," ",ii, fid, hdr.size, hdr.clusters, txtLen, dir );
					if ( (ourSuper->verbose&VERBOSE_HEADERS) )
						displayFileHeader(stdout, &hdr, 1|(ourSuper->verbose&VERBOSE_RETPTRS));
					fileBuff = (uint8_t*)malloc(hdr.clusters*512);
					if ( fileBuff )
					{
						if ( readFile((char *)dir, ourSuper, fileBuff, hdr.size, hdr.pointers[0]) == hdr.size)
						{
							ret |= iterateDir(nest+1,fileBuff,hdr.size,ourSuper, indexSys);
						}
						free(fileBuff);
					}
				}
				else
					printf("%*s%5d: 0x%08X REG 0x%08X/0x%08X 0x%02X %s\n", nest*4," ",ii, fid, hdr.size, hdr.clusters,txtLen, dir );
			}
			else
				printf("%*s%5d: 0x%08X REG 0x%02X %s *** FAILED to read file header\n", nest*4," ",ii, fid, txtLen, dir );
		}
		++ii;
		dir += txtLen;
	}
	return ret;
}
#endif

static void verifyFreemap(MgwfsSuper_t *ourSuper)
{
	int idx, rpIdx;
	int verbSave = ourSuper->verbose;
	MgwfsSuper_t super;
	MgwfsFoundFreeMap_t found;
	FsysRetPtr *ourFreeMap, *rp, *rpMax;
	uint32_t *indexPtr;
	MgwfsInode_t *inodePtr;
	FsysRetPtr tmp;
	
	/* Actually a cheat. We're using the freemap routines to
	   build a 'used' list instead of a freelist
	*/
	memset(&super,0,sizeof(super));
	memset(&found,0,sizeof(found));
	super.inodeList = (MgwfsInode_t *)calloc(FSYS_INDEX_FREE+1, sizeof(MgwfsInode_t));
	super.inodeList[FSYS_INDEX_FREE].fsHeader.size = 0;
	super.inodeList[FSYS_INDEX_FREE].fsHeader.clusters = 512;
	super.freeMapEntriesAvail = (super.inodeList[FSYS_INDEX_FREE].fsHeader.clusters*BYTES_PER_SECTOR)/sizeof(FsysRetPtr);
	ourFreeMap = (FsysRetPtr *)calloc(super.freeMapEntriesAvail,sizeof(FsysRetPtr));
	super.freeMap = ourFreeMap;
	/* Prepopulate the "used" list with the home block usage */
	for (idx=0; idx < FSYS_MAX_ALTS; ++idx)
	{
		ourFreeMap[idx].nblocks = 1;
		ourFreeMap[idx].start = FSYS_HB_ALG(idx,FSYS_HB_RANGE);
	}
	found.listAvailable = super.freeMapEntriesAvail;
	found.listUsed = FSYS_MAX_ALTS;
	ourSuper->verbose = 0;
	/* Populate the rest of the used list by reading all the file headers */
	indexPtr = ourSuper->indexSys;
	tmp.nblocks = 1;
	for (idx=0; idx < (ourSuper->indexSysHdr.size*BYTES_PER_SECTOR)/sizeof(uint32_t); idx += FSYS_MAX_ALTS, indexPtr += FSYS_MAX_ALTS)
	{
		if ( *indexPtr && !(*indexPtr&FSYS_EMPTYLBA_BIT) )
		{
			/* Add all the sectors of the file's header */
			for (rpIdx=0; rpIdx < FSYS_MAX_ALTS; ++rpIdx)
			{
				tmp.start = indexPtr[rpIdx];
				mgwfsFreeSectors(&super,&found,&tmp);
			}
		}
	}
	inodePtr = ourSuper->inodeList;
	for ( idx = 0; idx < ourSuper->numInodesAvailable; ++idx, ++inodePtr )
	{
		for (rpIdx=0; rpIdx < FSYS_MAX_ALTS; ++rpIdx)
		{
			/* Add all the sectors of the file's contents */
			rp = inodePtr->fsHeader.pointers[rpIdx];
			rpMax = rp+FSYS_MAX_FHPTRS;
			while ( rp < rpMax && rp->nblocks && rp->start )
			{
				if ( !mgwfsFreeSectors(&super,&found,rp) )
					break;
				++rp;
			}
		}
	}
	ourSuper->verbose = verbSave;
	dumpFreemap("Contents of \"used\" blocks before merge:", ourFreeMap, found.listUsed);
	rp = ourFreeMap;
	rpMax = ourFreeMap + super.freeMapEntriesAvail;
	/* Swap freemap pointer */
	/* ourFreeMap holds list of used sectors */
	/* found.listUsed says how many items there are */
	/* Make a local copy of the actual free map contents */
	super.freeMap = (FsysRetPtr *)malloc(ourSuper->freeMapEntriesAvail*sizeof(FsysRetPtr));
	memcpy(super.freeMap, ourSuper->freeMap, ourSuper->freeMapEntriesAvail*sizeof(FsysRetPtr));
	/* Copy the actual freemap.sys fileheader to local */
	super.inodeList[FSYS_INDEX_FREE].fsHeader = ourSuper->inodeList[FSYS_INDEX_FREE].fsHeader;
	found.listAvailable = ourSuper->freeMapEntriesAvail;
	found.listUsed = ourSuper->freeMapEntriesUsed;
	printf("Total entries available for used list: %ld, for free list: %d\n", rpMax-rp, found.listAvailable);
	while ( rp < rpMax && rp->start && rp->nblocks )
	{
		mgwfsFreeSectors(&super,&found,rp);
		++rp;
	}
	printf("Free'd a total of %ld used entries\nThe following should have just one entry from 0x01 to 0x%08X\n", rp - ourFreeMap, ourSuper->homeBlk.max_lba - 1);
	dumpFreemap("Contents of freemap.sys after merge:", super.freeMap, found.listUsed);
	free(super.freeMap);
	free(super.inodeList);
	free(ourFreeMap);
}

static int unpackDir(MgwfsSuper_t *ourSuper, MgwfsInode_t *inode, int nest)
{
	uint8_t *mem, *dirContents;
	int selfIdx, ret=0, *nextPtr;
	MgwfsInode_t *prevInodePtr, *child=NULL;
	static const char ErrTitle[] = "unpackDir(): ERROR:";
	
	if ( inode->fsHeader.type != FSYS_TYPE_DIR )
	{
		fprintf(ourSuper->logFile, "%sFile '%s' at inode %d is not a directory\n", ErrTitle, inode->fileName, inode->inode_no);
		return -1;
	}
	if ( inode->idxChildTop || inode->idxNextInode )
	{
		fprintf(ourSuper->logFile,"%sFile '%s' at inode %d has already been unpacked. Next=%d, children=%d\n",
				ErrTitle,
				inode->fileName, inode->inode_no,
				inode->idxNextInode,
				inode->idxChildTop);
		return -1;
	}
	mem = dirContents = (uint8_t *)malloc(inode->fsHeader.size);
	if ( !dirContents )
	{
		fprintf(ourSuper->logFile, "%sOut of memory allocating %d bytes to hold dir '%s' at inode %d\n",
				ErrTitle, inode->fsHeader.size, inode->fileName, inode->inode_no);
		return -1;
	}
	if ( readFile(inode->fileName, ourSuper, dirContents, inode->fsHeader.size, inode->fsHeader.pointers[0] ) < 0 )
	{
		fprintf(ourSuper->logFile, "%sFailed to read directory file '%s' at inode 0x%04X\n", ErrTitle, inode->fileName, inode->inode_no);
		free(dirContents);
		return -1;
	}
	selfIdx = inode->inode_no;
	prevInodePtr = inode;
	nextPtr = &inode->idxChildTop;
#if 0
	if ( (ourSuper->verbose&VERBOSE_UNPACK) )
		fprintf(ourSuper->logFile,"unpackDir(): %s .=0x%04X ..=0x%04X, idxNext=0x%04X, dirSize=%4d, %*s%s\n",
				S_ISDIR(inode->mode)?"DIR":"REG",
				selfIdx,
				dotDotIdx,
				inode->idxNextInode,
				inode->fsHeader.size,
				nest, "", inode->fileName);
#endif
	while ( dirContents < dirContents+inode->fsHeader.size )
	{
		int txtLen;
		uint8_t gen;
		uint32_t fid;

		fid = (dirContents[2]<<16)|(dirContents[1]<<8)|dirContents[0];
		dirContents += 3;
		gen = *dirContents++;
		txtLen = *dirContents++;
		if ( !fid )
		{
			if ( gen && txtLen )
			{
				fprintf(ourSuper->logFile, "%sFound file '%s' in dir '%s' with invalid fid: %d. fid must be 0 < fid < %d. (gen=%d, txtLen=%d) Skipped\n",
						ErrTitle,
						dirContents,
						inode->fileName,
						fid,
						ourSuper->numInodesAvailable,
						gen, txtLen
						);
				dirContents += txtLen;
				continue;
			}
			break;
		}
		if ( !txtLen )
		{
			fprintf(ourSuper->logFile, "%sFound file '%s' (fid %d) in dir '%s' with invalid txtLen of 0. Probably corrupted. Skipped rest of dir\n",
					ErrTitle, dirContents, fid, inode->fileName);
			break;
		}
		if ( fid >= ourSuper->numInodesAvailable )
		{
			fprintf(ourSuper->logFile, "%sFound file '%s' in dir '%s' with invalid fid: %d. fid must be 0 < fid < %d. Skipped\n",
					ErrTitle, dirContents, inode->fileName, fid, ourSuper->numInodesAvailable);
		}
		else
		{
			child = ourSuper->inodeList+fid;
			if ( child->fsHeader.generation != gen )
			{
				fprintf(ourSuper->logFile,"%sFound file '%s' (fid %d) in dir '%s' (inode %d) with bad generation. Expected %d, was %d. Skipped\n",
						ErrTitle,
						dirContents, fid,
						inode->fileName, inode->inode_no,
						gen,
						child->fsHeader.generation
						);
			}
			else if ( (strcmp((char *)dirContents,"..") && strcmp((char *)dirContents,".")) && (child->idxNextInode || child->idxChildTop || child->idxParentInode) )
			{
				fprintf(ourSuper->logFile,"%sFound file '%s' (fid %d) in dir '%s' (inode %d) already assigned to a different dir '%s' (inode %d). Parent=%d. Skipped\n",
						ErrTitle,dirContents, fid, inode->fileName, inode->inode_no,
						child->fileName, child->inode_no, child->idxParentInode);
			}
			else
			{
				int skip=0;
				if ( dirContents[0] == '.' )
				{
					if ( txtLen == 2 || (txtLen == 3 && dirContents[1] == '.') )
						skip = 1;
				}
				if ( !skip )
				{
					strncpy(child->fileName, (char *)dirContents, sizeof(child->fileName));
					child->idxParentInode = selfIdx;
					++inode->numInodes;
					if ( (ourSuper->verbose&VERBOSE_UNPACK) )
						fprintf(ourSuper->logFile,"unpackDir(): %s fid=%4d parent=%4d prev=%4d, %*s%s\n",
								S_ISDIR(child->mode)?"DIR":"REG",
								fid,
								child->idxParentInode,
								prevInodePtr->idxNextInode,
								nest, "", child->fileName);
					*nextPtr = fid;
					nextPtr = &child->idxNextInode;
					prevInodePtr = child;
					if ( S_ISDIR(child->mode) )
					{
						ret = unpackDir(ourSuper, child, nest + 2);
						if ( ret )
							return ret;
					}
				}
			}
		}
		dirContents += txtLen;
	}
	free(mem);
	return 0;
}

static int tree(MgwfsSuper_t *ourSuper, int topIdx, int nest)
{
	MgwfsInode_t *inode=ourSuper->inodeList+topIdx;
	int ret, nextIdx;
	
	do
	{
		fprintf(ourSuper->logFile, "%4d%*s%s%s\n", inode->inode_no, nest, " ", inode->fileName, S_ISDIR(inode->mode) ? "/" : "");
		if ( inode->idxChildTop )
		{
			ret = tree(ourSuper, inode->idxChildTop, nest+2 );
			if ( ret < 0 )
				return ret;
		}
		if ( !(nextIdx = inode->idxNextInode) )
			break;
		inode = ourSuper->inodeList+nextIdx;
		if ( nextIdx == inode->idxNextInode )
		{
			fprintf(ourSuper->logFile, "ERROR: Infinite loop. fid=%d, next=%d\n", inode->inode_no, inode->idxNextInode);
			break;
		}
	} while ( 1 );
	return 0;
}

static int getInode(MgwfsSuper_t *ourSuper, int topIdx, const char *path)
{
	char partPath[MGWFS_FILENAME_MAXLEN+1];
	MgwfsInode_t *inode;
	int ret=0, maxLen;
	const char *cp;
	
	partPath[sizeof(partPath)-1] = 0;
	if ( (ourSuper->verbose & VERBOSE_FUSE) )
	{
		fprintf(ourSuper->logFile,"getInode(): Looking for '%s' from top idx %d\n" ,path, topIdx);
		fflush(ourSuper->logFile);
	}
	if ( *path == '/' )
	{
		++path;
		if ( !*path )
		{
			if ( (ourSuper->verbose & VERBOSE_FUSE) )
			{
				fprintf(ourSuper->logFile,"\tgetInode(): of top dir '/' returned value of %d\n", topIdx );
				fflush(ourSuper->logFile);
			}
			return topIdx;
		}
		topIdx = ourSuper->inodeList[FSYS_INDEX_ROOT].idxChildTop;
	}
	cp = strchr(path,'/');
	if ( !cp )
	{
		strncpy(partPath,path,sizeof(partPath)-1);
		path += strlen(path);
	}
	else
	{
		maxLen = cp-path;
		if ( maxLen > (int)sizeof(partPath)-1 )
			maxLen = sizeof(partPath)-1;
		strncpy(partPath, path, maxLen);
		partPath[maxLen] = 0;
		path = cp+1;
	}
	inode = ourSuper->inodeList + topIdx;
	do
	{
		if ( (ourSuper->verbose & VERBOSE_FUSE) )
		{
			fprintf(ourSuper->logFile,"\tgetInode(): checking name '%s' against fileName '%s' (inode %d, next=%d)\n",
					partPath, inode->fileName, inode->inode_no, inode->idxNextInode);
			fflush(ourSuper->logFile);
		}
		if ( !strcmp(partPath, inode->fileName) )
		{
			ret = inode->inode_no;
			break;
		}
		if ( !inode->idxNextInode )
			break;
		inode = ourSuper->inodeList + inode->idxNextInode;
	} while (1);
	if ( ret && *path )
	{
		/* More stuff to look through. Though, this part has to be a directory */
		inode = ourSuper->inodeList + ret;
		if ( inode->idxChildTop )
			ret = getInode(ourSuper, inode->idxChildTop, path);
		else
		{
			if ( (ourSuper->verbose & VERBOSE_FUSE) )
			{
				fprintf(ourSuper->logFile,"\tgetInode(): More stuff to look through in '%s'. But inode %d is not a directory with anything in it.\n",
						path, ret );
				fflush(ourSuper->logFile);
			}
			ret = 0;
		}
	}
	if ( (ourSuper->verbose & VERBOSE_FUSE) )
	{
		fprintf(ourSuper->logFile,"\tgetInode(): returned value of %d\n", ret );
		fflush(ourSuper->logFile);
	}
	return ret;
}

static void *mgwfsf_init(struct fuse_conn_info *conn,
			struct fuse_config *cfg)
{
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfsf_init()\n");
		fflush(ourSuper.logFile);
	}
//	cfg->kernel_cache = 1;
	cfg->direct_io = 1;
	return NULL;
}

static int mgwfsf_getattr(const char *path, struct stat *stbuf,
			 struct fuse_file_info *fi)
{
	int idx;
	MgwfsInode_t *inode;
	
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfsf_getattr(path='%s',stbuf)\n", path);
		fflush(ourSuper.logFile);
	}
	memset(stbuf, 0, sizeof(struct stat));
	if ( (idx = getInode(&ourSuper, FSYS_INDEX_ROOT, path)) <= 0 )
		return -ENOENT;
	inode = ourSuper.inodeList + idx;
	if ( S_ISDIR(inode->mode) )
	{
		stbuf->st_mode = S_IFDIR | 0555;
		stbuf->st_nlink = 2 + inode->numInodes;
	}
	else
	{
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
	}
	stbuf->st_blksize = BYTES_PER_SECTOR;
	stbuf->st_blocks = inode->fsHeader.clusters;
	stbuf->st_ino = inode->inode_no;
	stbuf->st_ctime = inode->fsHeader.ctime;
	stbuf->st_mtime = inode->fsHeader.mtime;
	stbuf->st_size = inode->fsHeader.size;
	stbuf->st_gid = getgid();
	stbuf->st_uid = getuid();
	return 0;
}

static int mgwfsf_readdir(const char *path,
						  void *buf,
						  fuse_fill_dir_t filler,
						  off_t offset,
						  struct fuse_file_info *fi,
						  enum fuse_readdir_flags flags)
{
	MgwfsInode_t *inode;
	int idx, fRet;
	
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfsf_readdir(path='%s',buf=%p,offset=%ld,fi,flags=0x%X)\n", path, buf, offset,flags);
		fflush(ourSuper.logFile);
	}
	idx = getInode(&ourSuper,FSYS_INDEX_ROOT,path);
	if (!idx)
	{
		fprintf(ourSuper.logFile, "FUSE mgwfsf_readdir() returned -ENOENT because '%s' could not be found\n", path);
		fflush(ourSuper.logFile);
		return -ENOENT;
	}
	inode = ourSuper.inodeList+idx;
	if ( !S_ISDIR(inode->mode) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfsf_readdir() returned -ENOENT because '%s' (inode %d) is not a directory\n", path, idx);
		fflush(ourSuper.logFile);
		return -ENOENT;
	}
	filler(buf, ".", NULL, 0, FUSE_FILL_DIR_DEFAULTS);
	filler(buf, "..", NULL, 0, FUSE_FILL_DIR_DEFAULTS);
	idx = inode->idxChildTop;
	while ( idx )
	{
		struct stat stbuf;
		inode = ourSuper.inodeList+idx;
		memset(&stbuf, 0, sizeof(struct stat));
		if ( S_ISDIR(inode->mode) )
		{
			stbuf.st_mode = S_IFDIR | 0755;
			stbuf.st_nlink = 2 + inode->numInodes;
		}
		else
		{
			stbuf.st_mode = S_IFREG | 0444;
			stbuf.st_nlink = 1;
		}
		stbuf.st_blksize = BYTES_PER_SECTOR;
		stbuf.st_blocks = inode->fsHeader.clusters;
		stbuf.st_ino = inode->inode_no;
		stbuf.st_ctime = inode->fsHeader.ctime;
		stbuf.st_mtime = inode->fsHeader.mtime;
		stbuf.st_size = inode->fsHeader.size;
		stbuf.st_gid = getgid();
		stbuf.st_uid = getuid();
		fRet = filler(buf, inode->fileName, &stbuf, 0, FUSE_FILL_DIR_DEFAULTS);
		if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfsf_readdir(): Uploaded inode %d '%s'. Next=%d. fRet=%d\n", idx, inode->fileName, inode->idxNextInode, fRet );
			fflush(ourSuper.logFile);
		}
		if ( fRet )
			break;
		idx = inode->idxNextInode;
	}
	return 0;
}

static int mgwfsf_open(const char *path, struct fuse_file_info *fi)
{
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
		fprintf(ourSuper.logFile, "FUSE mgwfsf_open(path='%s',fi)\n", path);
#if 0
	if (strcmp(path+1, options.filename) != 0)
		return -ENOENT;

	if ((fi->flags & O_ACCMODE) != O_RDONLY)
		return -EACCES;

	return 0;
#else
	return -ENOENT;
#endif
}

static int mgwfsf_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfsf_read(path='%s',buf=%p,size=%ld,offset=%ld\n", path, buf,size,offset);
		fflush(ourSuper.logFile);
	}
#if 0
	size_t len;
	(void) fi;
	if(strcmp(path+1, options.filename) != 0)
		return -ENOENT;

	len = strlen(options.contents);
	if (offset < len) {
		if (offset + size > len)
			size = len - offset;
		memcpy(buf, options.contents + offset, size);
	} else
		size = 0;

	return size;
#else
	return -ENOENT;
#endif
}

/* Should be set to BYTES_PER_SECTOR, but it doesn't like that */
#define BLOCK_SIZE (4096)

static int mgwfsf_statfs(const char *path, struct statvfs *stp)
{
	FsysRetPtr *rp;
	
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfsf_statfs('%s',%p\n", path, stp);
		fflush(ourSuper.logFile);
	}
//	memset(stp,0,sizeof(statvfs));
	stp->f_type = ANON_INODE_FS_MAGIC;
	stp->f_bsize = BLOCK_SIZE; //BYTES_PER_SECTOR;
	stp->f_blocks = (ourSuper.homeBlk.max_lba*BYTES_PER_SECTOR)/BLOCK_SIZE;
	rp = ourSuper.freeMap;
	while ( rp && rp < ourSuper.freeMap + ourSuper.freeMapEntriesAvail && rp->nblocks )
	{
		stp->f_bfree += rp->nblocks;
		++rp;
	}
	stp->f_bavail = stp->f_blocks
		- (1+
		   (( ourSuper.inodeList[FSYS_INDEX_INDEX].fsHeader.size
			 +ourSuper.inodeList[FSYS_INDEX_FREE].fsHeader.size
			 +ourSuper.inodeList[FSYS_INDEX_ROOT].fsHeader.size
			 )
			+(BLOCK_SIZE-1)
			)/BLOCK_SIZE);
	stp->f_favail = ourSuper.numInodesAvailable;
	stp->f_ffree = stp->f_favail - ourSuper.numInodesUsed;
	stp->f_namemax = MGWFS_FILENAME_MAXLEN;
	stp->f_frsize = BLOCK_SIZE; //BYTES_PER_SECTOR;
	stp->f_flag = ST_NOSUID|ST_RDONLY; //ST_NOATIME|ST_NODIRATIME|ST_NOEXEC|;
	return 0;
}

static void helpEm(FILE *ofp, const char *progname)
{
	fprintf(ofp, "Usage: %s [options] <mountpoint>\n", progname);
	fprintf(ofp, "Filesystem specific options:\n"
		   "--log=<path>    Specify a path to a logfile (default=stdout)\n"
		   "--image=<path>  Specify a path to filesystem file (required)\n"
		   "--testpath=<path> Specify a test path into filesystem file (forces a -q)\n"
		   "--verbose=n 'n' is bit mask of verbose modes:\n"
		   "            May be expressed with normal C syntax [i.e. prefix 0x or 0b for hex or binary]:\n"
		   );
	fprintf(ofp, "    0x%04X = display some small details\n", VERBOSE_MINIMUM);
	fprintf(ofp, "    0x%04X = display home block\n", VERBOSE_HOME);
	fprintf(ofp, "    0x%04X = display file headers\n", VERBOSE_HEADERS);
	fprintf(ofp, "    0x%04X = display all retrieval pointers in file headers\n", VERBOSE_RETPTRS);
	fprintf(ofp, "    0x%04X = display attempts at file reads\n", VERBOSE_READ);
	fprintf(ofp, "    0x%04X = display contents of index.sys file\n", VERBOSE_INDEX);
	fprintf(ofp, "    0x%04X = display freemap primitive actions\n", VERBOSE_FREE);
	fprintf(ofp, "    0x%04X = display contents of freemap.sys file\n", VERBOSE_FREEMAP);
	fprintf(ofp, "    0x%04X = display and verify contents of freemap.sys file\n", VERBOSE_VERIFY_FREEMAP);
	fprintf(ofp, "    0x%04X = display contents of rootdir.sys file\n", VERBOSE_DMPROOT);
	fprintf(ofp, "    0x%04X = display details during unpack()\n", VERBOSE_UNPACK);
	fprintf(ofp, "    0x%04X = display directory tree\n", VERBOSE_ITERATE);
	fprintf(ofp, "    0x%04X = display anything FUSE related\n", VERBOSE_FUSE);
	fprintf(ofp, "    0x%04X = display FUSE function calls\n", VERBOSE_FUSE_CMD);
	fprintf(ofp, "-q        = quit before starting fuse stuff\n");
	fprintf(ofp, "-v        = sets verbose flag to a value of 0x001\n");
}

static const struct fuse_operations mgwfsf_oper = {
	.init       = mgwfsf_init,
	.getattr	= mgwfsf_getattr,
	.readdir	= mgwfsf_readdir,
	.open		= mgwfsf_open,
	.read		= mgwfsf_read,
	.statfs		= mgwfsf_statfs,
};

#define OPTION(t, p )                           \
    { t, offsetof(Options_t, p), 1 }
	
static const struct fuse_opt option_spec[] =
{
	OPTION( "--allocation=%lu", allocation ),
	OPTION( "--copies=%lu", copies ),
	OPTION( "--image=%s", image ),
	OPTION( "--testpath=%s", testPath ),
	OPTION( "--log=%s", logFile ),
	{ "--verbose", -1, FUSE_OPT_KEY_OPT},
	OPTION("-v", verbose ),
	OPTION("-h", show_help ),
	OPTION("--help", show_help ),
	OPTION("-q", quit ),
	OPTION("--quit", quit ),
	FUSE_OPT_END
};

static int procOption(void *data, const char *arg, int key, struct fuse_args *outargs)
{
	int ret=1;
	static const char Verbose[] = "--verbose";
	int sLen=sizeof(Verbose)-1;
	
//	printf("ProcOption('%s') checking for match\n", arg);
	if ( !strncmp(arg,Verbose,sLen) )
	{
		char *endp=NULL;
		if ( arg[sLen] == '=' )
			++sLen;
		options.verbose = strtoul(arg + sLen, &endp, 0);
		if ( !endp || *endp )
		{
			printf("Invalid argument on %s\n", arg);
			ret = -1;
		}
		ret = 0;
	}
//	printf("ProcOption('%s') returned %d\n", arg, ret);
	return ret;   // -1 on error, 0 if to toss, 1 if to keep
}

int main(int argc, char *argv[])
{
	int ii;
	int ret;
	uint32_t homeLbas[FSYS_MAX_ALTS], ckSum;
	MgwfsInode_t *inode;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	/* Parse options */
	if (fuse_opt_parse(&args, &options, option_spec, procOption) == -1)
		return 1;

	/* When --help is specified, first print our own file-system
	   specific help text, then signal fuse_main to show
	   additional help (by adding `--help` to the options again)
	   without usage: line (by setting argv[0] to the empty
	   string) */
	if ( !options.show_help && (!options.image || options.image[0] == 0) )
	{
		fprintf(stderr, "No image name provided. Requires a --image=<path> option\n");
		helpEm(stderr,argv[0]);
		return 1;
	}
	if (options.show_help) {
		helpEm(stderr,argv[0]);
		assert(fuse_opt_add_arg(&args, "--help") == 0);
		args.argv[0][0] = '\0';
	}
	if ( options.testPath )
		options.quit = 1;
	if ( options.logFile )
	{
		ourSuper.logFile = fopen(options.logFile,"w");
		if ( !ourSuper.logFile )
		{
			fprintf(stderr,"Error opening log file '%s': %s\n", options.logFile, strerror(errno));
			return 1;
		}
	}
	else
		ourSuper.logFile = stdout;
	fprintf(ourSuper.logFile, "Allocation=%ld, copies=%ld, verbose=0x%lX, imageName=%s, quit=%ld, logFile='%s'\n",
		   options.allocation, options.copies, options.verbose, options.image, options.quit, options.logFile);
	ourSuper.verbose = options.verbose;
	ourSuper.defaultAllocation = options.allocation;
	ourSuper.defaultCopies = options.copies;
	ourSuper.imageName = options.image;
	if ( !options.show_help )
	{
		if ( ourSuper.verbose )
		{
			fprintf(ourSuper.logFile, "__WORDSIZE=%d\n", __WORDSIZE);
			fprintf(ourSuper.logFile, "sizeof char=%ld, int=%ld, short=%ld, long=%ld, *=%ld, long long=%ld\n",
							sizeof(char), sizeof(int), sizeof(short), sizeof(long), sizeof(char *), sizeof(long long));
			fprintf(ourSuper.logFile, "sizeof unit8_t=%ld, uint16_t=%ld, uint32_t=%ld, uint64_t=%ld\n",
							sizeof(uint8_t), sizeof(uint16_t), sizeof(uint32_t), sizeof(uint64_t));
			fprintf(ourSuper.logFile, "llval=%llX, lval=%lX, int=%X\n", (long long)0x1234, (long)0x4567, (int)0x89AB);
			fprintf(ourSuper.logFile, "FSYS_FEATURES=0x%08X, FSYS_OPTIONS=0x%08X, FSYS_MAX_ALTS=%d, FSYS_MAX_FHPTRS=%ld\n", FSYS_FEATURES, FSYS_OPTIONS, FSYS_MAX_ALTS, FSYS_MAX_FHPTRS);
		}
		do
		{
			struct stat st;
			off64_t maxHb;
			off64_t sizeInSectors;
	
			if ( FSYS_MAX_ALTS != 3 )
			{
				fprintf(stderr, "FSYS_MAX_ALTS=%d. Application has to be built with it set to 3.\n", FSYS_MAX_ALTS);
				ret = -1;
				break;
			}
			ret = stat(ourSuper.imageName, &st);
			if ( ret < 0 )
			{
				fprintf(stderr,"Unable to stat '%s': %s\n", ourSuper.imageName, strerror(errno));
				break;
			}
			ourSuper.fd = open(ourSuper.imageName, O_RDONLY);
			if ( ourSuper.fd < 0 )
			{
				fprintf(stderr, "Error opening the '%s': %s\n", ourSuper.imageName, strerror(errno));
				ret = -1;
				break;
			}
	
			sizeInSectors = st.st_size/512;
			maxHb = sizeInSectors > FSYS_HB_RANGE ? FSYS_HB_RANGE:sizeInSectors;
			if ( (ourSuper.verbose&VERBOSE_HOME) )
			{
				fprintf(ourSuper.logFile, "File size 0x%lX, maxSector=0x%lX, maxHb=0x%lX\n", st.st_size, sizeInSectors, maxHb);
				fprintf(ourSuper.logFile, "Attempting to read a partition table that might be present\n");
			}
			if ( (sizeof(bootSect) != read(ourSuper.fd,&bootSect,sizeof(bootSect))) )
			{
				fprintf(stderr, "Failed to read boot sector: %s\n", strerror(errno));
				ret = -2;
				break;
			}
			for (ii=0; ii < 4; ++ii)
			{
				if ( bootSect.parts[ii].status == 0x80 && bootSect.parts[ii].type == 0x8f )
				{
					ourSuper.baseSector = bootSect.parts[ii].abs_sect;
					sizeInSectors = bootSect.parts[ii].num_sects;
					maxHb = sizeInSectors > FSYS_HB_RANGE ? FSYS_HB_RANGE:sizeInSectors;
					if ( (ourSuper.verbose&VERBOSE_HOME) )
						fprintf(ourSuper.logFile, "Found an agc fsys partition in partition %d. baseSector=0x%X, numSectors=0x%lX, maxHb=0x%lX\n", ii, ourSuper.baseSector, sizeInSectors, maxHb);
					break;
				}
			}
			if ( (ourSuper.verbose&VERBOSE_HOME) && ii >= 4 )
				fprintf(ourSuper.logFile, "No parition table found\n");
			for (ii=0; ii < FSYS_MAX_ALTS; ++ii)
				homeLbas[ii] = FSYS_HB_ALG(ii, maxHb);
			if ( !getHomeBlock(&ourSuper,homeLbas,maxHb,sizeInSectors,&ckSum) )
			{
				ret = -1;
				break;
			}
			if ( (ourSuper.verbose&VERBOSE_HOME) )
			{
				fprintf(ourSuper.logFile, "Home block:\n");
				displayHomeBlock(ourSuper.logFile,&ourSuper.homeBlk,ckSum);
			}
			if ( getFileHeader("index.sys", &ourSuper, FSYS_ID_INDEX, ourSuper.homeBlk.index, &ourSuper.indexSysHdr) )
			{
				if ( (ourSuper.verbose&VERBOSE_HEADERS) )
					displayFileHeader(ourSuper.logFile, &ourSuper.indexSysHdr, 1 | (ourSuper.verbose & VERBOSE_RETPTRS));
				ourSuper.numInodesAvailable = (ourSuper.indexSysHdr.clusters * 512) / (FSYS_MAX_ALTS * sizeof(uint32_t));
				ourSuper.indexSys = (uint32_t *)calloc(ourSuper.indexSysHdr.clusters * 512, 1);
				if ( readFile("index.sys", &ourSuper, (uint8_t*)ourSuper.indexSys, ourSuper.indexSysHdr.size, ourSuper.indexSysHdr.pointers[0]) < 0 )
				{
					fprintf(stderr,"Failed to read index.sys file\n");
					ret = -1;
					break;
				}
				if ( (ourSuper.verbose & VERBOSE_INDEX) )
				{
					if ( !(ourSuper.verbose&VERBOSE_HEADERS) )
						displayFileHeader(ourSuper.logFile,&ourSuper.indexSysHdr,1|(ourSuper.verbose&VERBOSE_RETPTRS));
					dumpIndex(ourSuper.indexSys, ourSuper.indexSysHdr.size);
				}
			}
			else
			{
				ret = -1;
				break;
			}
			/* First read all the fileheaders in the filesystem */
			/* Get some memory to hold all the local inodes */
			ourSuper.inodeList = (MgwfsInode_t *)calloc(ourSuper.numInodesAvailable, sizeof(MgwfsInode_t));
			if ( !ourSuper.inodeList )
			{
				fprintf(stderr, "Sorry. Not enough memory to hold %d inodes (%ld bytes)\n", ourSuper.numInodesAvailable, sizeof(MgwfsInode_t) * ourSuper.numInodesAvailable);
				close(ourSuper.fd);
				return 1;
			}
			inode = ourSuper.inodeList;
			memcpy(&inode->fsHeader, &ourSuper.indexSysHdr, sizeof(FsysHeader));
			++inode;
			ret = 0;
			for (ii=1; ii < ourSuper.numInodesAvailable; ++ii, ++inode)
			{
				char tmpName[32];
				uint32_t *lbas;
				
				lbas = ourSuper.indexSys + ii * FSYS_MAX_ALTS;
				if ( !*lbas )
					break;
				snprintf(tmpName,sizeof(tmpName),"Inode %d", ii);
				if ( !(*lbas & FSYS_EMPTYLBA_BIT) )
				{
					if ( getFileHeader(tmpName, &ourSuper, FSYS_ID_HEADER, lbas, &inode->fsHeader) )
					{
						inode->inode_no = ii;
						inode->mode = (inode->fsHeader.type == FSYS_TYPE_DIR) ? S_IFDIR|0555 : S_IFREG|0444;
						if ( (ourSuper.verbose&VERBOSE_HEADERS) )
							displayFileHeader(ourSuper.logFile, &inode->fsHeader, 1 | (ourSuper.verbose & VERBOSE_RETPTRS));
						else if ( (ourSuper.verbose&VERBOSE_MINIMUM) )
							fprintf(ourSuper.logFile, "Loaded file header (inode) %4d, lbas: 0x%08X 0x%08X 0x%08X. Type=0x%X (%s)\n",
								   ii,
								   lbas[0], lbas[1], lbas[2],
								   inode->fsHeader.type,
								   S_ISDIR(inode->mode) ? "DIR":"REG");
						++ourSuper.numInodesUsed;
					}
					else
					{
						ret = -1;
						break;
					}
				}
			}
			if ( ret < 0 )
				break;
			if ( (ourSuper.verbose&VERBOSE_MINIMUM) )
			{
				fprintf(ourSuper.logFile, "Inode info: inode size: %ld, inodesAvailable: %d, inodesUsed: %d\n", sizeof(MgwfsInode_t), ourSuper.numInodesAvailable, ourSuper.numInodesUsed);
			}
			/* The first 4 files don't actually belong to any directory and have no name, so fake it */
			inode = ourSuper.inodeList;
			for (ii=0; ii < 4; ++ii, ++inode)
			{
				static const char * const Names[] = 
				{
					"index.sys", "freemap.sys", "rootdir.sys", "journal.sys"
				};
				strncpy(inode->fileName, Names[ii], sizeof(inode->fileName));
				inode->fnLen = strlen(inode->fileName);
				inode->idxParentInode = FSYS_INDEX_ROOT;
				inode->inode_no = ii;
				inode->mode = (inode->fsHeader.type == FSYS_TYPE_DIR) ? S_IFDIR | 0555 : S_IFREG | 0444;
				/* But we need to read the contents of the freemap file */
				if ( ii == FSYS_INDEX_FREE )
				{
					ourSuper.freeMap = (FsysRetPtr *)calloc(inode->fsHeader.clusters * 512, 1);
					if ( readFile("freemap.sys", &ourSuper, (uint8_t *)ourSuper.freeMap, inode->fsHeader.size, inode->fsHeader.pointers[0]) < 0 )
					{
						fprintf(stderr,"Failed to read freemap.sys file\n");
						ret = -1;
						break;
					}
					if ( (ourSuper.verbose & (VERBOSE_FREEMAP | VERBOSE_VERIFY_FREEMAP)) )
					{
						dumpFreemap("Contents of freemap.sys before merge:", ourSuper.freeMap, (inode->fsHeader.size + sizeof(FsysRetPtr) - 1) / sizeof(FsysRetPtr));
						if ( (ourSuper.verbose & VERBOSE_VERIFY_FREEMAP) )
						{
							options.quit = 1;
							verifyFreemap(&ourSuper);
						}
					}
					else if ( (ourSuper.verbose&VERBOSE_MINIMUM) )
					{
						fprintf(ourSuper.logFile, "Loaded %ld slots (%d bytes) of freemap\n",
							   (inode->fsHeader.clusters * 512)/sizeof(FsysRetPtr),
							   inode->fsHeader.clusters * 512
							   );
					}
				}
			}
			if ( ret < 0 )
				break;
			inode = ourSuper.inodeList + FSYS_INDEX_ROOT; /* Point to the root directory */
			inode->idxParentInode = FSYS_INDEX_ROOT;
			unpackDir(&ourSuper, inode, 0); /* Create the entire filesystem directory tree */
			if ( (ourSuper.verbose&VERBOSE_ITERATE) )
				tree(&ourSuper, FSYS_INDEX_ROOT, 0 );
		} while ( 0 );
	}
	if ( ret >= 0 && options.testPath )
	{
		int idx = getInode(&ourSuper,FSYS_INDEX_ROOT,options.testPath);
		fprintf(ourSuper.logFile,"getInode('%s') returned %d\n", options.testPath, idx);
	}
	if ( ret >= 0 && !options.quit )
	{
		ret = fuse_main(args.argc, args.argv, &mgwfsf_oper, NULL);
		fuse_opt_free_args(&args);
	}
	if ( options.logFile )
		fclose(ourSuper.logFile);
	if ( ourSuper.fd >= 0 )
		close(ourSuper.fd);
	if ( ourSuper.indexSys )
		free( ourSuper.indexSys );
	if ( ourSuper.inodeList )
		free(ourSuper.inodeList);
	return ret;
}
