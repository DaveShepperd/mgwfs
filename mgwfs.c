/*
  mgwfs: Atari/MidwayGamesWest filesystem using libfuse: Filesystem in Userspace

  Copyright (C) 2025  Dave Shepperd <mgwfs@dshepperd.com>

  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.

 Build with enclosed Makefile

*/

#include "mgwfs.h"

BootSector_t bootSect;
MgwfsSuper_t ourSuper = { .ourMutex=PTHREAD_MUTEX_INITIALIZER };

Options_t options;

void displayFileHeader(FILE *outp, FsysHeader *fhp, int retrievalsToo)
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

void displayHomeBlock(FILE *outp, const FsysHomeBlock *homeBlkp, uint32_t cksum)
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
	fprintf(outp,
			   "    def_extend:%d\n"
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
	fprintf(outp,
		   "    boot1[]:   0x%08X, 0x%08X, 0x%08X\n"
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

int countSectors(FsysRetPtr *rp, int maxRps, uint32_t *totalSectors)
{
	uint32_t sectors=0;
	int ii;
	for ( ii = 0; rp->nblocks && ii < maxRps; ++rp, ++ii )
		sectors += rp->nblocks;
	if ( totalSectors )
		*totalSectors = sectors;
	return ii;
}

static int getHBSector(MgwfsSuper_t *ourSuper, off64_t sector, FsysHomeBlock *hb, uint32_t *ckSumP)
{
	int jj, fd = ourSuper->fd;
	size_t sts;
	uint32_t options, cksum, *csp;
	
	if ( (ourSuper->verbose&VERBOSE_HOME) )
		fprintf(ourSuper->logFile,"Attempting to read home block at sector 0x%lX\n", sector);
	if ( lseek64(fd,sector*512,SEEK_SET) == (off64_t)-1 )
	{
		fprintf(ourSuper->errFile,"Failed to seek to sector 0x%lX: %s\n", sector, strerror(errno));
		return 1;
	}
	sts = read(fd, hb, sizeof(FsysHomeBlock));
	if ( sts != sizeof(FsysHomeBlock) )
	{
		fprintf(ourSuper->errFile,"Failed to read %ld byte home block at sector 0x%lX: %s\n",
				sizeof(FsysHomeBlock),
				sector,
				strerror(errno));
		return 2;
	}
	cksum = 0;
	csp = (uint32_t *)hb;
	for (jj=0; jj < sizeof(FsysHomeBlock)/sizeof(uint32_t); ++jj)
		cksum += *csp++;
	options = hb->features & hb->options & (FSYS_FEATURES_CMTIME | FSYS_FEATURES_EXTENSION_HEADER | FSYS_FEATURES_ABTIME);
	if (    hb->id == FSYS_ID_HOME
		 && hb->rp_major == 1
		 && hb->rp_minor == 1
		 && !cksum
		 && options == FSYS_FEATURES_CMTIME
		 && hb->fh_size == (int)sizeof(FsysHeader)
		 && hb->maxalts == FSYS_MAX_ALTS
	   )
	{
		*ckSumP = cksum;
		return 0;
	}
	return 3;
}

int getHomeBlock(MgwfsSuper_t *ourSuper, off64_t maxHbSector, off64_t diskSizeInSectors, uint32_t *ckSumP)
{
	FsysHomeBlock *homeBlkp = &ourSuper->homeBlk;
	FsysHomeBlock lclHomes[FSYS_MAX_ALTS], *lclHome=lclHomes;
	int ii, good=0, match=0;
	off64_t sector, altSector=256+1;
	
	memset(lclHomes,0,sizeof(lclHomes));
	*ckSumP = 0;
	for (ii=0; ii < FSYS_MAX_ALTS; ++ii, ++lclHome)
	{
		int res;
		sector = ourSuper->homeLbas[ii]+ourSuper->baseSector;
		res = getHBSector(ourSuper,sector,lclHome,ckSumP);
		if ( !res )
		{
			good |= 1<<ii;
		}
		else
		{
			if ( ii && res == 3 && lclHomes[0].hb_major == 1 && lclHomes[0].hb_minor == 1 )
			{
				/* old systems require a scan for the next HB */
				while ( (sector=altSector) < diskSizeInSectors )
				{
					ourSuper->homeLbas[ii] = sector;
					altSector += 256;
					res = getHBSector(ourSuper,sector,lclHome,ckSumP);
					if ( !res )
					{
						good |= 1<<ii;
						break;
					}
				}
				if ( res )
				{
					int jj;
					for (jj=ii; jj < FSYS_MAX_ALTS; ++jj)
						ourSuper->homeLbas[jj] = 0;
					fprintf(ourSuper->errFile, "Failed to find Home block %d\n", ii);
					break;
				}
			}
			else
			{
				fprintf(ourSuper->errFile, "Home block %d is not what is expected:\n", ii);
				displayHomeBlock(ourSuper->errFile, lclHome, *ckSumP);
				continue;
			}
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
				fprintf(ourSuper->errFile,"Header %d does not match header 0\n", ii);
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

int getFileHeader(const char *title, MgwfsSuper_t *ourSuper, uint32_t id, uint32_t lbas[FSYS_MAX_ALTS], FsysHeader *fhp)
{
	FsysHeader lclHdrs[FSYS_MAX_ALTS], *lclFhp=lclHdrs;
	int fd, ii, good=0, match=0;
	ssize_t sts;
	off64_t sector;
	uint32_t sectors;
	
	memset(lclHdrs,0,sizeof(lclHdrs));
	fd = ourSuper->fd;
	for (ii=0; ii < FSYS_MAX_ALTS; ++ii, ++lclFhp)
	{
		sector = lbas[ii]+ourSuper->baseSector;
		if ( (ourSuper->verbose&VERBOSE_HEADERS) )
			fprintf(ourSuper->logFile,"Attempting to read file header for '%s' at sector 0x%lX\n", title, sector);
		if ( lseek64(fd,sector*512,SEEK_SET) == (off64_t)-1 )
		{
			fprintf(ourSuper->errFile,"Failed to seek to sector 0x%lX: %s\n", sector, strerror(errno));
			continue;
		}
		sts = read(fd, lclFhp, sizeof(FsysHeader));
		if ( sts != sizeof(FsysHeader) )
		{
			fprintf(ourSuper->errFile,"Failed to read %ld byte file header at sector 0x%lX: %s\n", sizeof(FsysHeader), sector, strerror(errno));
			continue;
		}
		if ( lclFhp->id != id )
		{
			fprintf(ourSuper->errFile, "Sector at 0x%lX is not a file header:\n", sector);
			displayFileHeader(ourSuper->errFile,lclFhp,0);
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
				fprintf(ourSuper->errFile,"Header %d does not match header 0\n", ii);
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
		for (ii=0; ii < FSYS_MAX_ALTS; ++ii)
		{
			countSectors(fhp->pointers[ii], FSYS_MAX_FHPTRS, &sectors);
			++sectors;	/* Account for file header */
			ourSuper->sectorsFree -= sectors;
			ourSuper->sectorsUsed += sectors;
		}
	}
	else
	{
		memset(fhp,0,sizeof(FsysHeader));
		return 0;
	}
	return 1;
}

int readFile(const char *title,  MgwfsSuper_t *ourSuper, uint8_t *dst, int bytes, FsysRetPtr *retPtr)
{
	off64_t sector;
	int fd, ptrIdx=0, retSize=0, blkLimit;
	ssize_t rdSts, limit;
	
	fd = ourSuper->fd;
	while ( retSize < bytes )
	{
		if ( !retPtr->start || !retPtr->nblocks  )
		{
			fprintf(ourSuper->errFile,"Empty retrieval pointer at retIdx %d while reading '%s'\n", ptrIdx, title);
			return -1;
		}
		sector = retPtr->start;
		blkLimit = retPtr->nblocks;
		limit = bytes - retSize;
		if ( blkLimit*BYTES_PER_SECTOR > limit )
			blkLimit = ((blkLimit*BYTES_PER_SECTOR-limit)+BYTES_PER_SECTOR-1)/512;
		if ( (ourSuper->verbose&VERBOSE_READ) )
		{
			fprintf(ourSuper->logFile,"Attempting to read %ld bytes for %s. ptrIdx=%d, sector=0x%X, nblocks=%d (limited blocks=%d)\n",
			   limit, title, ptrIdx, retPtr->start, retPtr->nblocks, blkLimit);
		}
		if ( lseek64(fd,(sector+ourSuper->baseSector)*BYTES_PER_SECTOR,SEEK_SET) == (off64_t)-1 )
		{
			fprintf(ourSuper->errFile,"Failed to seek to sector 0x%lX: %s\n", sector, strerror(errno));
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
			fprintf(ourSuper->errFile,"Failed to read %ld bytes for %s. Instead got %ld: %s\n", limit, title, rdSts, strerror(errno));
			return -1;
		}
		retSize += rdSts;
	}
	return retSize;
}

int flushFile(const char *title,  MgwfsSuper_t *ourSuper, FuseFH_t *fhp)
{
	return -EIO;
}

static int writeWholeFile(const char *title,  MgwfsSuper_t *ourSuper, FuseFH_t *fhp)
{
	off64_t sector;
	int alts, rps, ptrIdx=0, retSize=0, blkLimit;
	ssize_t limit;
	FsysRetPtr *retPtr;
	MgwfsFoundFreeMap_t stuff;
	MgwfsInode_t *inode;
	int bytes;
	uint32_t sectors;
//	const uint8_t *src = fhp->rwBuff;
//	ssize_t rdSts;
//	int fd = ourSuper->fd;
	
	sectors = (fhp->rwBuffSize+BYTES_PER_SECTOR-1)/BYTES_PER_SECTOR;
	bytes = sectors*BYTES_PER_SECTOR;
	inode = ourSuper->inodeList[fhp->inode];
	/* We only write copy 0, so free sectors used by duplicate files, if any */
	for ( alts = 1; alts < FSYS_MAX_ALTS; ++alts )
	{
		for (rps=0; rps < FSYS_MAX_FHPTRS; ++rps)
		{
			retPtr = inode->fsHeader.pointers[alts]+rps;
			if ( !retPtr->nblocks )
				break;
			mgwfsFreeSectors(ourSuper,NULL,retPtr);
			retPtr->nblocks = 0;
			retPtr->start = 0;
		}
	}
	/* If the file has gotten bigger, free existing retrieval pointers to copy 0 */
	if ( sectors > inode->fsHeader.clusters )
	{
		for (rps=0; rps < FSYS_MAX_FHPTRS; ++rps)
		{
			retPtr = inode->fsHeader.pointers[0]+rps;
			if ( !retPtr->nblocks )
				break;
			mgwfsFreeSectors(ourSuper,NULL,retPtr);
			retPtr->nblocks = 0;
			retPtr->start = 0;
		}
		/* Next, allocate all the necessary RP's for the entire file. */
		memset(&stuff,0,sizeof(stuff));
		ptrIdx = 0;
		retPtr = inode->fsHeader.pointers[0] + 0;
		inode->fsHeader.clusters = sectors;
		while ( sectors > 0 )
		{
			if ( ptrIdx >= FSYS_MAX_FHPTRS )
			{
				fprintf(ourSuper->errFile,"%s: Ran out room for retrieval pointers for '%s'\n",
						title,
						inode->fileName
						);
				return -ENOSPC;
			}
			if ( !mgwfsFindFree(ourSuper, &stuff, sectors) )
			{
				fprintf(ourSuper->errFile,"%s: Ran out of room trying to allocate %d sectors for '%s'\n",
						title,
						sectors,
						inode->fileName
						);
				return -ENOSPC;
			}
			retPtr = inode->fsHeader.pointers[0] + ptrIdx;
			retPtr->nblocks = stuff.result.nblocks;
			retPtr->start = stuff.result.start;
			sectors -= stuff.result.nblocks;
			++ptrIdx;
		}
	}
	/* write the file to disk */
	retPtr = inode->fsHeader.pointers[0] + 0;
	ptrIdx = 0;
	retSize = 0;
	while ( retSize < bytes )
	{
		sector = retPtr->start;
		blkLimit = retPtr->nblocks;
		limit = bytes - retSize;
		if ( blkLimit*BYTES_PER_SECTOR > limit )
			blkLimit = ((blkLimit*BYTES_PER_SECTOR-limit)+BYTES_PER_SECTOR-1)/512;
#if 0
		if ( (ourSuper->verbose&VERBOSE_READ) )
		{
			fprintf(ourSuper->logFile,"%s: Attempting to write %ld bytes for %s. ptrIdx=%d, sector=0x%X, nblocks=%d (limited blocks=%d)\n",
					title, limit, inode->fileName, ptrIdx, retPtr->start, retPtr->nblocks, blkLimit);
		}
		if ( lseek64(fd,(sector+ourSuper->baseSector)*BYTES_PER_SECTOR,SEEK_SET) == (off64_t)-1 )
		{
			fprintf(ourSuper->errFile, "%s: Failed to seek to sector 0x%lX on %s: %s\n", title, sector, inode->fileName, strerror(errno));
			return -1;
		}
		if ( limit > retPtr->nblocks*BYTES_PER_SECTOR )
		{
			limit = retPtr->nblocks*BYTES_PER_SECTOR;
			++retPtr;
			++ptrIdx;
		}
		rdSts = write(fd, src+retSize, limit);
		if ( rdSts != limit )
		{
			fprintf(ourSuper->errFile,"%s: Failed to write %ld bytes to  %s. Instead got %ld: %s\n",
					title, limit, inode->fileName, rdSts, strerror(errno));
			return -EIO;
		}
		retSize += rdSts;
#else
		if ( limit > retPtr->nblocks*BYTES_PER_SECTOR )
		{
			limit = retPtr->nblocks*BYTES_PER_SECTOR;
			++retPtr;
			++ptrIdx;
		}
		if ( (ourSuper->verbose&VERBOSE_READ) )
		{
			fprintf(ourSuper->logFile,"%s: Would have written %ld sectors at sector 0x%08lX for %s.\n",
					title,
					limit/BYTES_PER_SECTOR,
					sector,
					inode->fileName);
		}
		retSize += limit;
#endif
	}
	inode->fsHeader.size = retSize;
	inode->fsHeader.mtime = time(NULL);
	mgwfsAddToDirty(ourSuper, inode->inode_no);
	return retSize;
}

int fileOpen(const char *title, const char *path, MgwfsSuper_t *ourSuper, FuseFH_t *fhp)
{
	/* Nothing to do here yet. So just return 0 */
	return 0;
}

int fileFlush(const char *title, MgwfsSuper_t *ourSuper, FuseFH_t *fhp)
{
	/* Nothing to do here yet. So just return 0 */
	return 0;
}

int fileClose(const char *title, MgwfsSuper_t *ourSuper, FuseFH_t *fhp)
{
	if ( (fhp->openFlags&(O_RDWR|O_WRONLY)) )
	{
		int sts;
		MgwfsInode_t *inode = ourSuper->inodeList[fhp->inode];
		inode->fsHeader.mtime = time(NULL);
		sts = writeWholeFile(title,ourSuper,fhp);
		if ( sts >= 0 )
			sts = 0;
	}
	return 0;
}

int fileRename(const char *title, MgwfsSuper_t *ourSuper, const char *oldPath, const char *newPath)
{
	return -EIO;
}

//int fileRead(const char *title, MgwfsSuper_t *ourSuper, FuseFH_t *fhp, off_t offset, size_t bytes)
//{
//	if ( (fhp->openFlags&(O_WRONLY|O_RDWR)) )
//		return -EIO;
//	return 0;
//}

MgwfsInode_t *findUnusedInode(MgwfsSuper_t *ourSuper)
{
	int idx;
	MgwfsInode_t *inode;
	
	for ( idx = FSYS_INDEX_JOURNAL + 1; idx < ourSuper->numInodesUsed; ++idx )
	{
		if ( !ourSuper->inodeList[idx] )
			break;
	}
	if ( idx >= ourSuper->numInodesAvailable )
	{
		MgwfsInode_t **inodePtr;
#define INODE_ADDS (FSYS_DEFAULT_EXTEND*BYTES_PER_SECTOR/(sizeof(uint32_t)*FSYS_MAX_ALTS))
		int newNum = ourSuper->numInodesAvailable+INODE_ADDS;

		inodePtr = (MgwfsInode_t **)realloc(ourSuper->inodeList, newNum*sizeof(MgwfsInode_t));
		if ( !inodePtr )
		{
			fprintf(ourSuper->logFile, "findUnusedInode(): failed to allocate %ld bytes for more inodes\n", newNum*sizeof(MgwfsInode_t));
			fflush(ourSuper->logFile);
			return NULL;
		}
		ourSuper->numInodesAvailable = newNum;
		ourSuper->inodeList = inodePtr;
		if ( (ourSuper->verbose&(VERBOSE_WRITES)) )
			fprintf(ourSuper->logFile, "findUnusedInode(): Added an additional inode to list. inodesAvailable was %d, now is %d\n", ourSuper->numInodesUsed, ourSuper->numInodesUsed+1);
		++ourSuper->numInodesUsed;
	}
	else
	{
		if ( (ourSuper->verbose&(VERBOSE_WRITES)) )
			fprintf(ourSuper->logFile, "findUnusedInode(): Reused inode %d.\n", idx);
	}
	inode = (MgwfsInode_t *)calloc(1,sizeof(MgwfsInode_t));
	inode->inode_no = idx;
	inode->fsHeader.ctime = time(NULL);
	inode->fsHeader.id = FSYS_ID_HEADER;
	ourSuper->inodeList[idx] = inode;
	return inode;
}

int fileExtend(const char *title, MgwfsSuper_t *ourSuper, FuseFH_t *fhp)
{
	MgwfsInode_t *inode;
	MgwfsFoundFreeMap_t fMap;
	FsysRetPtr *rp, *rp1;
	int lastRp;
	
	inode = ourSuper->inodeList[fhp->inode];
	rp = inode->fsHeader.pointers[0];
	rp1 = rp+1;
	lastRp = 0;
	if ( rp->nblocks )
	{
		for ( lastRp = 1; lastRp < FSYS_MAX_FHPTRS; ++lastRp, ++rp1, ++rp )
		{
			if ( !rp1->nblocks )
				break;
		}
		if ( lastRp >= FSYS_MAX_FHPTRS )
		{
			fprintf(ourSuper->logFile, "fileExtend(): No more retrieval pointers to extend file '%s'. Returned ENOSPC\n", inode->fileName);
			return -ENOSPC;
		}
	}
	memset(&fMap,0,sizeof(fMap));
	fMap.hints = rp;
	if ( !mgwfsFindFree(ourSuper, &fMap, ourSuper->homeBlk.def_extend) )
	{
		fprintf(ourSuper->logFile,"fileExtend(): No room to extend file '%s' by %d sectors. Returned ENOSPC\n", inode->fileName, ourSuper->homeBlk.def_extend);
		return -ENOSPC;
	}
	inode->fsHeader.clusters += ourSuper->homeBlk.def_extend;
	if ( fMap.result.start != rp->start )
	{
		rp1->start = fMap.result.start;
		rp1->nblocks = fMap.result.nblocks;
		if ( (ourSuper->verbose&(VERBOSE_WRITES)) )
			fprintf(ourSuper->logFile, "%s: fileExtend('%s'). Added a new retrival pointer at %d: 0x%X/0x%X\n", title, inode->fileName, lastRp, rp1->start, rp1->nblocks);
	}
	else
	{
		if ( (ourSuper->verbose&(VERBOSE_WRITES)) )
		{
			fprintf(ourSuper->logFile, "%s: fileExtend('%s'). Changed retrival pointer at %d: from 0x%X/0x%X to 0x%X/0x%X\n",
					title,
					inode->fileName,
					lastRp,
					rp->start, rp->nblocks,
					fMap.result.start, fMap.result.nblocks);
		}
		rp->nblocks = fMap.result.nblocks;
	}
	return 0;
}

static void insertIntoDir(MgwfsSuper_t *ourSuper, uint32_t dirTop , MgwfsInode_t *fileInode)
{
	MgwfsInode_t *dirInode;
	MgwfsInode_t *inode;
	uint32_t idx;
	
	dirInode = ourSuper->inodeList[dirTop];
	if ( dirInode && (idx=dirInode->idxChildTop) )
	{
		while ( idx )
		{
			int cmp;
			
			inode = ourSuper->inodeList[idx];
			if ( !inode )
				return;
			if ( strcmp(inode->fileName, ".") && strcmp(inode->fileName, "..") )
			{
				cmp = strcmp(fileInode->fileName, inode->fileName);
				if ( cmp < 0 )
				{
					MgwfsInode_t *prev = ourSuper->inodeList[inode->idxPrevInode];
					prev->idxNextInode = fileInode->inode_no;
					inode->idxPrevInode = fileInode->inode_no;
					fileInode->idxPrevInode = prev->inode_no;
					fileInode->idxNextInode = inode->idxNextInode;
					fileInode->idxParentInode = dirTop;
					return;
				}
			}
			idx = inode->idxNextInode;
		}
	}
}

int fileCreate(const char *title, const char *path,  MgwfsSuper_t *ourSuper)
{
	int sLen;
	uint32_t idx;
	MgwfsInode_t *inode;
	char *dir;
	char *tmpDir;
	
	if ( (ourSuper->verbose&(VERBOSE_FUSE|VERBOSE_FUSE_CMD)) )
		fprintf(ourSuper->logFile, "%s: fileCreate() path='%s'\n", title, path);
	inode = findUnusedInode(ourSuper);
	if ( !inode )
	{
		fprintf(ourSuper->logFile, "%s: fileCreate() for '%s' failed to find empty inode, returned ENOSPC\n", title, path);
		return -ENOSPC;
	}
	if ( *path == '/' )
		++path;
	sLen = strlen(path)+1;
	tmpDir = (char *)malloc(sLen);
	strncpy(tmpDir,path,sLen);
	dir = strrchr(tmpDir,'/');
	if ( dir )
		*dir = 0;
	else
		dir = tmpDir;
	idx = findInode(ourSuper, FSYS_INDEX_ROOT, tmpDir);
	insertIntoDir(ourSuper,idx,inode);
	fflush(ourSuper->logFile);
	free(tmpDir);
	return 0;
}

int fileWrite(const char *title, MgwfsSuper_t *ourSuper, FuseFH_t *fhp, off_t offset, size_t bytes)
{
	return -EIO;
}

void dumpIndex(FILE *outp, uint32_t *indexBase, int bytes)
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
	fprintf(outp,"Contents of index.sys:\n");
	while ( index < indexBase+(bytes+sizeof(uint32_t)-1)/sizeof(uint32_t) )
	{
		fprintf(outp, "    %5d: 0x%08X 0x%08X 0x%08X", ii, index[0], index[1], index[2]);
		if ( ii < 4 )
			fprintf(outp, " (%s)", Titles[ii]);
		fprintf(outp, "\n");
		++ii;
		index += 3;
	}
}

int dumpFreemap(FILE *outp, const char *title, FsysRetPtr *rpBase, int maxEntries, uint32_t *totalSectors )
{
	FsysRetPtr *rp = rpBase;
	int ii=0;
	int32_t freeSize=0;
	
	if ( title )
		fprintf(outp, "%s\n", title);
	while ( rp < rpBase + maxEntries )
	{
		freeSize += rp->nblocks;
		fprintf(outp, "    %5d: 0x%08X-0x%08X (%d)\n", ii, rp->start, rp->nblocks ? rp->start+rp->nblocks-1:rp->start, rp->nblocks);
		if ( !rp->start || !rp->nblocks )
			break;
		++ii;
		++rp;
	}
	fprintf(outp, "    Total size: %d sectors\n", freeSize);
	if ( totalSectors )
		*totalSectors = freeSize;
	return ii;
}

void dumpDir(FILE *outp, uint8_t *dirBase, int bytes, MgwfsSuper_t *ourSuper, uint32_t *indexSys )
{
	uint8_t *dir = dirBase;
	int fd, ii=0;
	FsysHeader hdr;
	
	fprintf(outp, "Contents of rootdir.sys:\n");
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
		fprintf(outp, "    %5d: 0x%08X 0x%02X %3d %s\n", ii, fid, gen, txtLen, dir );
		if ( fd > 0 && indexSys )
		{
			if ( getFileHeader((char *)dir, ourSuper, FSYS_ID_HEADER, indexSys + fid * FSYS_MAX_ALTS, &hdr) )
				displayFileHeader(ourSuper->logFile,&hdr,1|(ourSuper->verbose&VERBOSE_RETPTRS));
		}
		++ii;
		dir += txtLen;
	}
}

void verifyFreemap(MgwfsSuper_t *ourSuper)
{
	int idx, rpIdx;
	int verbSave = ourSuper->verbose;
	MgwfsSuper_t tmpSuper;
//	MgwfsFoundFreeMap_t found;
	FsysRetPtr *ourUsedMap, *rp, *rpMax;
	uint32_t *indexPtr, mlstrt;
	MgwfsInode_t *inodePtr, **inodePtrPtr;
	FsysRetPtr tmp, *missingList, *mlptr;
	int missingListSize;
	uint32_t totalFreeSectors, totalUsedSectors, totalMergedSectors, totalFakeFree=0;
	
	/* Actually a cheat. We're using the freemap routines to
	   build a 'used' list instead of a freelist
	*/
	fprintf(ourSuper->logFile, "Total entries available in free map: %d, Used in free map: %d\n", ourSuper->freeMapEntriesAvail, ourSuper->freeMapEntriesUsed);
	fprintf(ourSuper->logFile, "Total sectors free %d, used %d, lost %d\n", ourSuper->sectorsFree, ourSuper->sectorsUsed, ourSuper->sectorsLost);
	idx = dumpFreemap(ourSuper->logFile, "Contents of freemap before merge:", ourSuper->freeMap, ourSuper->freeMapEntriesAvail, &totalFreeSectors);
	fprintf(ourSuper->logFile,"Total free sectors: %d, idx=%d, freeMapUsed=%d %s\n",
			totalFreeSectors,
			idx,
			ourSuper->freeMapEntriesUsed,
			idx==ourSuper->freeMapEntriesUsed ? "MATCH":" ***DIFFERENT*** ");
	memset(&tmpSuper,0,sizeof(tmpSuper));
//	memset(&found,0,sizeof(found));
	tmpSuper.verbose = ourSuper->verbose;
	ourSuper->verbose = 0;
	tmpSuper.logFile = ourSuper->logFile;
	tmpSuper.errFile = ourSuper->errFile;
	tmpSuper.inodeList = (MgwfsInode_t **)calloc(FSYS_INDEX_FREE+1, sizeof(MgwfsInode_t *));
	tmpSuper.inodeList[FSYS_INDEX_INDEX] = (MgwfsInode_t *)calloc(1, sizeof(MgwfsInode_t));
//	tmpSuper.inodeList[FSYS_INDEX_INDEX]->fsHeader.size = 0;
	tmpSuper.inodeList[FSYS_INDEX_FREE] = (MgwfsInode_t *)calloc(1, sizeof(MgwfsInode_t));
	tmpSuper.inodeList[FSYS_INDEX_FREE]->fsHeader.size = 0;
	tmpSuper.inodeList[FSYS_INDEX_FREE]->fsHeader.clusters = 512;
	tmpSuper.freeMapEntriesAvail = 2*(tmpSuper.inodeList[FSYS_INDEX_FREE]->fsHeader.clusters*BYTES_PER_SECTOR)/sizeof(FsysRetPtr);
	ourUsedMap = (FsysRetPtr *)calloc(tmpSuper.freeMapEntriesAvail,sizeof(FsysRetPtr));
	tmpSuper.freeMap = ourUsedMap;
	tmpSuper.freeMapEntriesUsed = 0;
	tmp.nblocks = 1;
	/* Prepopulate the "used" list with the home block usage */
	for (idx=0; idx < FSYS_MAX_ALTS; ++idx)
	{
		if ( (tmp.start = ourSuper->homeLbas[idx]) )
			mgwfsFreeSectors(&tmpSuper,NULL,&tmp);
	}
	/* Populate the rest of the used list by reading all the file headers */
	indexPtr = ourSuper->indexSys;
	for (idx=0; idx < ourSuper->indexSysHdr.size/sizeof(uint32_t); idx += FSYS_MAX_ALTS, indexPtr += FSYS_MAX_ALTS)
	{
		if ( (*indexPtr&FSYS_LBA_MASK) && !(*indexPtr&FSYS_EMPTYLBA_BIT) )
		{
			/* Add all the sectors of the file's header */
			for (rpIdx=0; rpIdx < FSYS_MAX_ALTS; ++rpIdx)
			{
				tmp.start = indexPtr[rpIdx];
				mgwfsFreeSectors(&tmpSuper,NULL,&tmp);
			}
		}
	}
	inodePtrPtr = ourSuper->inodeList;
	for ( idx = 0; idx < ourSuper->numInodesAvailable; ++idx )
	{
		if ( !(inodePtr = *inodePtrPtr++) )
			break;
		for (rpIdx=0; rpIdx < FSYS_MAX_ALTS; ++rpIdx)
		{
			/* Add all the sectors of the file's contents */
			rp = inodePtr->fsHeader.pointers[rpIdx];
			rpMax = rp+FSYS_MAX_FHPTRS;
			while ( rp < rpMax && rp->nblocks && rp->start )
			{
				mgwfsFreeSectors(&tmpSuper,NULL,rp);
				++rp;
			}
		}
	}
	ourSuper->verbose = verbSave;
	idx = dumpFreemap(ourSuper->logFile, "Contents of \"used\" blocks before merge:", ourUsedMap, tmpSuper.freeMapEntriesAvail, &totalUsedSectors);
	fprintf(ourSuper->logFile,"Total used sectors: %d, idx=%d, freeMapUsed=%d %s\n",
			totalUsedSectors,
			idx,
			tmpSuper.freeMapEntriesUsed,
			idx==tmpSuper.freeMapEntriesUsed ? "MATCH":" ***DIFFERENT*** ");
	fprintf(ourSuper->logFile,"Total of free+used: %d. Total missing: %d\n",
			totalFreeSectors+totalUsedSectors,
			ourSuper->homeBlk.max_lba-1-(totalFreeSectors+totalUsedSectors)
			);
	rp = ourUsedMap;
	rpMax = ourUsedMap + tmpSuper.freeMapEntriesAvail;
	missingListSize = 0;
	missingList = NULL;
	mlptr = NULL;
	mlstrt = 1;
	if ( rp->start == 1 )
	{
		mlstrt = rp->start+rp->nblocks;
		++rp;
	}
	while ( rp < rpMax && rp->nblocks )
	{
		if ( !mlptr || mlptr >= missingList+missingListSize-2 )
		{
			int mlidx = mlptr-missingList;
			missingListSize += 1024;
			missingList = (FsysRetPtr *)realloc(missingList,missingListSize*sizeof(FsysRetPtr));
			mlptr = missingList + mlidx;
		}
		mlptr->nblocks = rp->start-mlstrt;
		mlptr->start = mlstrt;
		++mlptr;
		mlstrt = rp->start+rp->nblocks;
		++rp;
	}
	if ( mlstrt < ourSuper->homeBlk.max_lba-1 && mlptr )
	{
		mlptr->nblocks = ourSuper->homeBlk.max_lba-1-mlstrt;
		mlptr->start = mlstrt;
		++mlptr;
	}
	if ( mlptr )
	{
		mlptr->nblocks = 0;
		mlptr->start = 0;
		++mlptr;
		dumpFreemap(ourSuper->logFile, "Contents of \"fake\" free blocks before merge:", missingList, missingListSize, &totalFakeFree);
		free(missingList);
		missingList = NULL;
	}
	idx = rp-ourUsedMap;
	/* Swap freemap pointer */
	/* tmpSuper.freeMapEntriesUsed says how many items there are in the "used" list */
	/* Make a local copy of the actual free map contents */
	tmpSuper.freeMapEntriesAvail = ourSuper->freeMapEntriesAvail+idx;
	tmpSuper.freeMap = (FsysRetPtr *)malloc(ourSuper->freeMapEntriesAvail * sizeof(FsysRetPtr));
	memcpy(tmpSuper.freeMap, ourSuper->freeMap, ourSuper->freeMapEntriesAvail*sizeof(FsysRetPtr));
	tmpSuper.freeMapEntriesUsed = ourSuper->freeMapEntriesUsed;
	/* Copy the actual freemap.sys fileheader to local */
	tmpSuper.inodeList[FSYS_INDEX_FREE]->fsHeader = ourSuper->inodeList[FSYS_INDEX_FREE]->fsHeader;
	/* ourUsedMap holds list of used sectors */
	fprintf(ourSuper->logFile, "Total of used entries list: %d, total of free entries: %d, total of potential both used+free: %d, total available: %d\n",
			idx,
			tmpSuper.freeMapEntriesUsed,
			idx + tmpSuper.freeMapEntriesUsed,
			tmpSuper.freeMapEntriesAvail);
	rp = ourUsedMap;
	rpMax = ourUsedMap + tmpSuper.freeMapEntriesAvail;
	/* Walk the list of used sectors and free them */
	while ( rp < rpMax && rp->start && rp->nblocks )
	{
		mgwfsFreeSectors(&tmpSuper,NULL,rp);
		++rp;
	}
	idx = rp-ourUsedMap;
	fprintf(ourSuper->logFile, "Free'd a total of %d used entries\nThe following should have just one entry from 0x01 to 0x%08X\n",
			idx,
			ourSuper->homeBlk.max_lba - 1);
	idx = dumpFreemap(ourSuper->logFile, "Contents of freemap.sys after merge:", tmpSuper.freeMap, tmpSuper.freeMapEntriesAvail, &totalMergedSectors);
	fprintf(ourSuper->logFile,"Total merged sectors: %d, idx=%d, freeMapUsed=%d %s\n",
			totalFreeSectors,
			idx,
			tmpSuper.freeMapEntriesUsed,
			idx==tmpSuper.freeMapEntriesUsed ? "MATCH":" ***DIFFERENT*** ");
	fprintf(ourSuper->logFile, "Total free sectors %u, used sectors %u, (total=%u), merged sectors %u (0x%X; diff=%d). Maxlba 0x%X (%d). Lost sectors %d\n",
			totalFreeSectors,
			totalUsedSectors,
			totalFreeSectors+totalUsedSectors,
			totalMergedSectors,
			totalMergedSectors,
			totalFreeSectors+totalUsedSectors-totalMergedSectors,
			ourSuper->homeBlk.max_lba,
			ourSuper->homeBlk.max_lba,
			ourSuper->homeBlk.max_lba-1-totalMergedSectors
			);
	free(tmpSuper.freeMap);
	free(ourUsedMap);
	free(tmpSuper.inodeList[FSYS_INDEX_INDEX]);
	free(tmpSuper.inodeList[FSYS_INDEX_FREE]);
	free(tmpSuper.inodeList);
}

/* This function will unpack a directory and create a linked list of MgwfsInode_t inodes contained therein */
int unpackDir(MgwfsSuper_t *ourSuper, MgwfsInode_t *inode, int nest)
{
	uint8_t *mem, *dirContents;
	int selfIdx, ret=0, *nextPtr, prevIdx;
	MgwfsInode_t *prevInodePtr, *child=NULL;
	static const char ErrTitle[] = "unpackDir(): ERROR:";
	
	if ( inode->fsHeader.type != FSYS_TYPE_DIR )
	{
		/* The inode has to be a directory type else give up */
		fprintf(ourSuper->logFile, "%sFile '%s' at inode %d is not a directory\n", ErrTitle, inode->fileName, inode->inode_no);
		return -1;
	}
	if ( inode->idxChildTop || inode->idxNextInode )
	{
		/* This function has already been called with this inode so give up */
		fprintf(ourSuper->logFile,"%sFile '%s' at inode %d has already been unpacked. Next=%d, children=%d\n",
				ErrTitle,
				inode->fileName, inode->inode_no,
				inode->idxNextInode,
				inode->idxChildTop);
		return -1;
	}
	if ( nest > MGWFS_MAX_NEST_LEVEL*2 )
	{
		/* Sanity check on recusion */
		fprintf(ourSuper->logFile,"%sDirectory '%s' at inode %d is nested too deep. Nest=%d\n",
				ErrTitle,
				inode->fileName, inode->inode_no,
				nest/2);
		return -1;
	}
	/* Get a buffer big enough to hold the on-disk directory contents */
	mem = dirContents = (uint8_t *)malloc(inode->fsHeader.size);
	if ( !dirContents )
	{
		fprintf(ourSuper->logFile, "%sOut of memory allocating %d bytes to hold dir '%s' at inode %d\n",
				ErrTitle, inode->fsHeader.size, inode->fileName, inode->inode_no);
		return -1;
	}
	/* Fill the buffer with the file contents */
	if ( readFile(inode->fileName, ourSuper, dirContents, inode->fsHeader.size, inode->fsHeader.pointers[0] ) < 0 )
	{
		fprintf(ourSuper->logFile, "%sFailed to read directory file '%s' at inode 0x%04X\n", ErrTitle, inode->fileName, inode->inode_no);
		free(dirContents);
		return -1;
	}
	selfIdx = inode->inode_no;      /* Get the index of the caller's inode */
	prevInodePtr = inode;			/* remember the current inode pointer  */
	nextPtr = &inode->idxChildTop;	/* point to place to put index should this inode be itself a directory */
	prevIdx = 0;					/* There's no previous for the first entry in this directory */
	while ( dirContents < dirContents+inode->fsHeader.size )
	{
		int txtLen;
		uint8_t gen;
		uint32_t fid;

		/* Pickup the index to the inode */
		fid = (dirContents[2]<<16)|(dirContents[1]<<8)|dirContents[0];
		dirContents += 3;
		/* Get the generation number */
		gen = *dirContents++;
		/* Get the null terminated filename length */
		txtLen = *dirContents++;
		/* If the index is 0, it's nfg */
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
		/* If the length is 0, it technically means it should be 256, but we're declaring it an error since there never were any names that long */
		if ( !txtLen )
		{
			fprintf(ourSuper->logFile, "%sFound file '%s' (fid %d) in dir '%s' with invalid txtLen of 0. Probably corrupted. Skipped rest of dir\n",
					ErrTitle, dirContents, fid, inode->fileName);
			break;
		}
		/* If the index is outside the limit provided for in the index.sys file, it's nfg */
		if ( fid >= ourSuper->numInodesAvailable )
		{
			fprintf(ourSuper->logFile, "%sFound file '%s' in dir '%s' with invalid fid: %d. fid must be 0 < fid < %d. Skipped\n",
					ErrTitle, dirContents, inode->fileName, fid, ourSuper->numInodesAvailable);
		}
		else
		{
			/* Point to the MgwfsInode_t assigned to this file */
			child = ourSuper->inodeList[fid];
			if ( child->fsHeader.generation != gen )
			{
				/* The generation number doesn't match, so this entry is nfg */
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
				/* The inode "belongs" to a different directory. We don't allow that, so this entry is nfg */
				fprintf(ourSuper->logFile,"%sFound file '%s' (fid %d) in dir '%s' (inode %d) already assigned to a different dir '%s' (inode %d). Parent=%d. Skipped\n",
						ErrTitle,dirContents, fid, inode->fileName, inode->inode_no,
						child->fileName, child->inode_no, child->idxParentInode);
			}
			else
			{
				int skip=0;
				if ( dirContents[0] == '.' )
				{
					/* Ignore the files '.' and '..' listed in the tree */
					if ( txtLen == 2 || (txtLen == 3 && dirContents[1] == '.') )
						skip = 1;
				}
				if ( !skip )
				{
					/* Copy the filename into our inode */
					strncpy(child->fileName, (char *)dirContents, MGWFS_FILENAME_MAXLEN);
					if ( (ourSuper->verbose&VERBOSE_DMPROOT) && !nest )
					{
						fprintf(ourSuper->logFile,"rootdir.sys: gen %02X, fid=0x%06X, len=%3d, %s\n",
								gen,
								fid,
								txtLen,
								child->fileName);
					}
					/* Record index of the directory that this file belongs to into the child */
					child->idxParentInode = selfIdx;
					/* Count the number of child inodes in this directory */
					++inode->numInodes;
					if ( (ourSuper->verbose&VERBOSE_UNPACK) )
						fprintf(ourSuper->logFile,"unpackDir(): %s fid=%4d parent=%4d prev=%4d, %*s%s\n",
								S_ISDIR(child->mode)?"DIR":"REG",
								fid,
								child->idxParentInode,
								prevInodePtr->idxNextInode,
								nest, "", child->fileName);
					/* Tell previous which is next */
					*nextPtr = fid;
					/* Tell current what was previous */
					child->idxPrevInode = prevIdx;
					/* Remember the previous for next time */
					prevIdx = fid;
					/* Remember previous place in order place it's next */
					nextPtr = &child->idxNextInode;
					prevInodePtr = child;
					if ( S_ISDIR(child->mode) )
					{
						/* If current child is a directory, recurse */
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

int tree(MgwfsSuper_t *ourSuper, int topIdx, int nest)
{
	MgwfsInode_t *inode=ourSuper->inodeList[topIdx];
	int ret, nextIdx;
	
	if ( !inode )
	{
		fprintf(stderr,"tree(): Called with topIdx %d pointing to null inode, nest=%d\n", topIdx, nest);
		return -1;
	}
	do
	{
		fprintf(ourSuper->logFile, "%4d%4d%*s%s%s\n", inode->inode_no, inode->idxPrevInode, nest, " ", inode->fileName, S_ISDIR(inode->mode) ? "/" : "");
		if ( inode->idxChildTop )
		{
			ret = tree(ourSuper, inode->idxChildTop, nest+2 );
			if ( ret < 0 )
				return ret;
		}
		if ( !(nextIdx = inode->idxNextInode) )
			break;
		inode = ourSuper->inodeList[nextIdx];
		if ( nextIdx == inode->idxNextInode )
		{
			fprintf(ourSuper->logFile, "ERROR: Infinite loop. fid=%d, next=%d\n", inode->inode_no, inode->idxNextInode);
			break;
		}
	} while ( 1 );
	return 0;
}

int findInode(MgwfsSuper_t *ourSuper, int topIdx, const char *path)
{
	char partPath[MGWFS_FILENAME_MAXLEN+1];
	MgwfsInode_t *inode;
	int ret=0, maxLen;
	const char *cp;
	
	partPath[sizeof(partPath)-1] = 0;
	if ( (ourSuper->verbose & VERBOSE_LOOKUP) )
	{
		fprintf(ourSuper->logFile,"getInode(): Looking for '%s' from top idx %d\n" ,path, topIdx);
		fflush(ourSuper->logFile);
	}
	if ( *path == '/' )
	{
		++path;
		if ( !*path )
		{
			if ( (ourSuper->verbose & VERBOSE_LOOKUP) )
			{
				fprintf(ourSuper->logFile,"\tgetInode(): of top dir '/' returned value of %d\n", topIdx );
				fflush(ourSuper->logFile);
			}
			return topIdx;
		}
		topIdx = ourSuper->inodeList[FSYS_INDEX_ROOT]->idxChildTop;
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
	inode = ourSuper->inodeList[topIdx];
	do
	{
		if ( (ourSuper->verbose & VERBOSE_LOOKUP_ALL) )
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
		inode = ourSuper->inodeList[inode->idxNextInode];
	} while (1);
	if ( ret && *path )
	{
		/* More stuff to look through. Though, this part has to be a directory */
		inode = ourSuper->inodeList[ret];
		if ( inode->idxChildTop )
			ret = findInode(ourSuper, inode->idxChildTop, path);
		else
		{
			if ( (ourSuper->verbose & VERBOSE_LOOKUP) )
			{
				fprintf(ourSuper->logFile,"\tgetInode(): More stuff to look through in '%s'. But inode %d is not a directory with anything in it.\n",
						path, ret );
				fflush(ourSuper->logFile);
			}
			ret = 0;
		}
	}
	if ( (ourSuper->verbose & VERBOSE_LOOKUP) )
	{
		fprintf(ourSuper->logFile,"\tgetInode(): returned value of %d\n", ret );
		fflush(ourSuper->logFile);
	}
	return ret;
}

#define FUSEFH_INCREMENTS (64)		/* Number of new FuseFH_t structures to get at one time */

static FuseFH_t *getNewFuseFHidx(MgwfsSuper_t *ourSuper)
{
	FuseFH_t *fhp, *nFhp;
	int idx, num;
	
	if ( (fhp=ourSuper->fuseFHs) )
	{
		for (idx=0; idx < ourSuper->numFuseFHs; ++fhp)
		{
			if ( !fhp->index )
			{
				if ( fhp->rwBuff )
					free(fhp->rwBuff);
				memset(fhp, 0, sizeof(FuseFH_t));
				fhp->index = idx + 1;
				return fhp;
			}
		}
	}
	idx = ourSuper->numFuseFHs;
	num = idx+FUSEFH_INCREMENTS;
	fhp = (FuseFH_t *)realloc(ourSuper->fuseFHs,num*sizeof(FuseFH_t));
	ourSuper->fuseFHs = fhp;
	ourSuper->numFuseFHs = num;
	nFhp = fhp + idx;
	memset(nFhp,0,FUSEFH_INCREMENTS*sizeof(FuseFH_t));	/* Preclear all the newly added structs */
	nFhp->index = idx+1;
	return nFhp;
}

FuseFH_t *getFuseFHidx(MgwfsSuper_t *ourSuper, uint64_t idx)
{
	if ( !idx )
		return getNewFuseFHidx(ourSuper);
	return ourSuper->fuseFHs+(idx-1);
}

void freeFuseFHidx(MgwfsSuper_t *ourSuper, uint64_t idx)
{
	if ( idx )
	{
		FuseFH_t *fhp = ourSuper->fuseFHs + (idx - 1);
		if ( fhp->rwBuff )
			free(fhp->rwBuff);
		memset(fhp,0,sizeof(FuseFH_t));
	}
}

int writeHomeBlock(MgwfsSuper_t *super)
{
	return EIO;
}

int writeIndexSys(MgwfsSuper_t *super)
{
	return EIO;
}

int writeFreeMapSys(MgwfsSuper_t *super)
{
	return EIO;
}

int writeFileHeader(MgwfsSuper_t *super, MgwfsInode_t *inode)
{
	return EIO;
}

int writeDirectory(MgwfsSuper_t *super, MgwfsInode_t *dir)
{
	return EIO;
}

