/*
  mgwfs: Atari/MidwayGamesWest filesystem using libfuse: Filesystem in Userspace

  Copyright (C) 2025  Dave Shepperd <mgwfs@dshepperd.com>

  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.

 Build with enclosed Makefile

*/

#include "mgwfs.h"

BootSector_t bootSect;
MgwfsSuper_t ourSuper;

Options_t options;

#if !NO_MUTEXES
static pthread_mutex_t wrMutex = PTHREAD_MUTEX_INITIALIZER;

void mgwfs_destroy_mutex(void)
{
	pthread_mutex_destroy(&wrMutex);
}

#if DEBUG_LOCKS
void mgwfs_lock_it(const char *name, MgwfsSuper_t *ourSuper, pthread_mutex_t *mutex, const char *fileName, int lineNo)
{
	if ( (ourSuper->verbose&VERBOSE_LOCKS) )
	{
		fprintf(ourSuper->logFile, "mgwfs_lock_it() %s:%d LOCKing %s\n", fileName, lineNo, name);
		fflush(ourSuper->logFile);
	}
	pthread_mutex_lock(mutex);
}

void mgwfs_unlock_it(const char *name, MgwfsSuper_t *ourSuper, pthread_mutex_t *mutex, const char *fileName, int lineNo)
{
	if ( (ourSuper->verbose&VERBOSE_LOCKS) )
	{
		fprintf(ourSuper->logFile, "mgwfs_unlock_it() %s:%d UNLOCKing %s\n", fileName, lineNo, name);
		fflush(ourSuper->logFile);
	}
	pthread_mutex_unlock(mutex);
}
#endif	/* DEBUG_LOCKS */
#endif	/* NO_MUTEXES */

void displayFileHeader(FILE *outp, FsysHeader *fhp, int retrievalsToo)
{
	FsysRetPtr *rp;
	int alt,ii;
	
	fprintf(outp,  "    id:        0x%X\n"
			       "    size:      %d (sizeof FsysHeader: %ld)\n"
				   "    clusters:  %d\n"
				   "    generation:%d\n"
				   "    type:      %d\n"
				   "    flags:     0x%04X\n"
				   "    ctime:     %d\n"
				   "    mtime:     %d\n"
			, fhp->id
			, fhp->size
			, sizeof(FsysHeader)
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
		   "    hb_size:   %d (sizeof FsysHomeBlock: %ld)\n"
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
		   ,sizeof(FsysHomeBlock)
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
					if (ourSuper->errFile != stderr)
						fprintf(stderr, "Failed to find Home block %d\n", ii);
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

int getFileHeader(const char *title, MgwfsSuper_t *ourSuper, uint32_t id, IndexSys_t *lbas, FsysHeader *fhp)
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
		sector = lbas->lba[ii] + ourSuper->baseSector;
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
			ourSuper->freeMap.sectorsFree -= sectors;
			ourSuper->freeMap.sectorsUsed += sectors;
		}
	}
	else
	{
		memset(fhp,0,sizeof(FsysHeader));
		return 0;
	}
	return 1;
}

int readWholeFile(const char *title,  MgwfsSuper_t *ourSuper, uint8_t *dst, int bytes, FsysRetPtr *retPtr)
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

#if 0
static void dmpDirty(const char *title1, const char *title2, MgwfsSuper_t *ourSuper, int haveRetVal, int retVal)
{
	if ( (ourSuper->verbose&VERBOSE_WRITES) )
	{
		char tmpBuf[265];
		int ii, len;
		
		ii = ourSuper->numDirtyInodes;
		ii += (ourSuper->specialDirtys&SPECIAL_DIRTY_INDEX)?1:0;
		ii += (ourSuper->specialDirtys&SPECIAL_DIRTY_FREE)?1:0;
		len = snprintf(tmpBuf,sizeof(tmpBuf),"%s %s idx=%d ",
					   title1, title2, ii );
		if ( ourSuper->dirtyInodes )
		{
			for ( ii = 0; ii < ourSuper->numDirtyInodes; ++ii )
				len += snprintf(tmpBuf + len, sizeof(tmpBuf) - len, " %d", ourSuper->dirtyInodes[ii]);
		}
		if ( (ourSuper->specialDirtys&SPECIAL_DIRTY_INDEX) )
			len += snprintf(tmpBuf + len, sizeof(tmpBuf) - len, " %d", FSYS_INDEX_INDEX);
		if ( (ourSuper->specialDirtys&SPECIAL_DIRTY_FREE) )
			len += snprintf(tmpBuf + len, sizeof(tmpBuf) - len, " %d", FSYS_INDEX_FREE);
		if ( haveRetVal )
			len += snprintf(tmpBuf + len, sizeof(tmpBuf) - len, " returned %d", retVal);
		fprintf(ourSuper->logFile, "%s\n", tmpBuf);
		fflush(ourSuper->logFile);
	}
}
#else
#define dmpDirty(a,b,c,d,e) do { ; } while (0)
#endif

void addToDirty(const char *title, MgwfsSuper_t *ourSuper, int idx)
{
	int ii, *dInodes;
	
	LOCK_IT("wrMutex", ourSuper, &wrMutex);
	if ( idx == FSYS_INDEX_INDEX || idx == FSYS_INDEX_FREE )
	{
		do
		{
			if ( (ourSuper->verbose & VERBOSE_WRITES) )
			{
				if (    (idx == FSYS_INDEX_INDEX && (ourSuper->specialDirtys&SPECIAL_DIRTY_INDEX))
					 || (idx == FSYS_INDEX_FREE && (ourSuper->specialDirtys&SPECIAL_DIRTY_FREE))
				   )
				{
					break;
				}
			}
			if ( (idx == FSYS_INDEX_INDEX && !(ourSuper->specialDirtys&SPECIAL_DIRTY_INDEX)) )
			{
				ourSuper->specialDirtys |= SPECIAL_DIRTY_INDEX;
				break;
			}
			if ( (idx == FSYS_INDEX_FREE && !(ourSuper->specialDirtys&SPECIAL_DIRTY_FREE)) )
			{
				ourSuper->specialDirtys |= SPECIAL_DIRTY_FREE;
				break;
			}
		} while ( 0 );
		dmpDirty("addToDirty()", title, ourSuper, 0, 0);
		UNLOCK_IT("wrMutex", ourSuper, &wrMutex);
		return;
	}
	if ( (dInodes=ourSuper->dirtyInodes) )
	{
		for ( ii = 0; ii < ourSuper->numDirtyInodes; ++ii, ++dInodes )
		{
			if ( *dInodes == idx )
			{
				dmpDirty("addToDirty()", title, ourSuper, 0, 0);
				UNLOCK_IT("wrMutex", ourSuper, &wrMutex);
				return;		// Already in list. Nothing to do.
			}
		}
	}
#define ADD_TO_INODE_INCREMENT (1024)
	if ( !(dInodes=ourSuper->dirtyInodes) || ourSuper->numDirtyInodes >= ourSuper->numDirtyInodesAvailable )
	{
		int *newPtr, newCnt;
		newCnt = ourSuper->numDirtyInodesAvailable + ADD_TO_INODE_INCREMENT;
		newPtr = (int *)realloc(dInodes, newCnt*sizeof(int));
		if ( dInodes && (ourSuper->verbose&VERBOSE_WRITES) )
		{
			fprintf(ourSuper->logFile,"addToDirty(): Ran out of entries. Have %d, bumping to %d.\n",
					ourSuper->numDirtyInodesAvailable, newCnt);
			fflush(ourSuper->logFile);
		}
		if ( !newPtr )
		{
			fprintf(ourSuper->logFile,"addToDirty(): Out of memory trying to add %d entries\n", newCnt);
			fflush(ourSuper->logFile);
			return;
		}
		ourSuper->dirtyInodes = newPtr;
		ourSuper->numDirtyInodesAvailable = newCnt;
		dInodes = newPtr;
	}
	ourSuper->dirtyInodes[ourSuper->numDirtyInodes] = idx;
	++ourSuper->numDirtyInodes;
	dmpDirty("addToDirty()", title, ourSuper, 0, 0);
	UNLOCK_IT("wrMutex", ourSuper, &wrMutex);
}

static int popFmDirty(MgwfsSuper_t *ourSuper)
{
	int nxt = -1;
	LOCK_IT("wrMutex",ourSuper,&wrMutex);
	if ( ourSuper->numDirtyInodes )
	{
		nxt = ourSuper->dirtyInodes[0];
		memmove(ourSuper->dirtyInodes + 0, ourSuper->dirtyInodes + 1, (ourSuper->numDirtyInodes-1)*sizeof(int32_t));
		--ourSuper->numDirtyInodes;
	}
	if ( nxt < 0 && !(ourSuper->specialDirtys&SPECIAL_DIRTY_NEST) )
	{
		if ( (ourSuper->specialDirtys & SPECIAL_DIRTY_INDEX) )
		{
			ourSuper->specialDirtys &= ~SPECIAL_DIRTY_INDEX;
			nxt = FSYS_INDEX_INDEX;
		}
		else if ( (ourSuper->specialDirtys & SPECIAL_DIRTY_FREE) )
		{
			ourSuper->specialDirtys &= ~SPECIAL_DIRTY_FREE;
			nxt = FSYS_INDEX_FREE;
		}
	}
	dmpDirty("popFmDirty()","",ourSuper,1,nxt);
	UNLOCK_IT("wrMutex",ourSuper,&wrMutex);
	return nxt;
}

int flushFile(const char *title,  MgwfsSuper_t *ourSuper, FuseFH_t *fhp)
{
	return -EIO;
}

static int allocateFHSectors( MgwfsSuper_t *ourSuper, MgwfsInode_t *inode, IndexSys_t *fhLBA)
{
	MgwfsFoundFreeMap_t stuff;
	FsysRetPtr tmpRPs[FSYS_MAX_ALTS];
	int altIdx;

	memset(&stuff,0,sizeof(stuff));
	for (altIdx=0; altIdx < FSYS_MAX_ALTS; ++altIdx)
	{
		stuff.minSector = FSYS_HB_ALG(altIdx, ourSuper->maxHb);
		if ( !mgwfsFindFree(ourSuper,&stuff,1,0) )
		{
			int ii;
			for (ii=0; ii < altIdx; ++ii)
				mgwfsFreeSectors(ourSuper,tmpRPs+ii,0);
			return -ENOSPC;
		}
		tmpRPs[altIdx].nblocks = stuff.result.nblocks;
		tmpRPs[altIdx].start = stuff.result.start;
	}
	for (altIdx=0; altIdx < FSYS_MAX_ALTS; ++altIdx)
	{
		fhLBA->lba[altIdx] = tmpRPs[altIdx].start;
		/* Keep the inode's own copy of its header LBAs in sync: writeFileHeader
		 * reads from indexSys[] (fhLBA), but the index.sys file is rebuilt from
		 * inode->fhSectors, so both must carry the newly allocated sectors or
		 * the inode won't be findable after a remount. */
		inode->fhSectors[altIdx] = tmpRPs[altIdx].start;
	}
	addToDirty("allocFHSectors()", ourSuper, FSYS_INDEX_INDEX);
	addToDirty("allocFHSectors()", ourSuper, FSYS_INDEX_FREE);
	return 0;
}

/*
 * Ensure every copy (alternate) of 'inode' has retrieval pointers covering at
 * least 'sectors' sectors, allocating from the freemap as needed. A single
 * mgwfsFindFree() call can return fewer sectors than requested (fragmented free
 * space, or a contiguous extend that runs into an occupied region), so we loop:
 * each round grows the trailing RP when the new chunk abuts it, or starts a
 * fresh RP otherwise, until the copy is fully covered or we run out of space /
 * retrieval-pointer slots. Returns 0 on success or a negative errno.
 */
static int allocateRPSectors( const char *title, MgwfsSuper_t *ourSuper, MgwfsInode_t *inode, RwBuff_t *rwBuff, int sectors)
{
	int altIdx, alts=0;

	(void)rwBuff;
	/* Count the copies already present; fall back to the default for a new file. */
	for (altIdx=0; altIdx < FSYS_MAX_ALTS; ++altIdx)
	{
		if ( inode->fsHeader.pointers[altIdx][0].nblocks )
			++alts;
	}
	if ( alts < 1 )
		alts = ourSuper->defaultCopies;
	if ( alts < 1 )
		alts = 1;
	for (altIdx=0; altIdx < alts; ++altIdx)
	{
		FsysRetPtr *rps = inode->fsHeader.pointers[altIdx];
		int rpIdx, have;

		/* How many sectors does this copy already cover, and where's the first
		 * free RP slot? */
		have = 0;
		for (rpIdx=0; rpIdx < FSYS_MAX_FHPTRS && rps[rpIdx].nblocks; ++rpIdx)
			have += rps[rpIdx].nblocks;
		while ( have < sectors )
		{
			MgwfsFoundFreeMap_t fMap;
			FsysRetPtr *lastRP = rpIdx ? &rps[rpIdx-1] : NULL;
			int added;

			memset(&fMap, 0, sizeof(fMap));
			if ( lastRP )
			{
				/* Offer the trailing RP as a hint so a contiguous chunk just
				 * extends it instead of consuming another RP slot. */
				fMap.hint.start = lastRP->start;
				fMap.hint.nblocks = lastRP->nblocks;
			}
			fMap.minSector = FSYS_COPY_ALG(altIdx, ourSuper->maxHb);
			if ( !mgwfsFindFree(ourSuper, &fMap, sectors - have, 0) )
			{
				fprintf(ourSuper->logFile,"%s: allocateRPSectors(): No room to grow file '%s' to %d sectors (copy %d had %d). Returned ENOSPC\n",
						title, inode->fileName, sectors, altIdx, have);
				return -ENOSPC;
			}
			if ( lastRP && fMap.result.start == lastRP->start )
			{
				/* Contiguous: the trailing RP simply grew. */
				added = fMap.result.nblocks - lastRP->nblocks;
				lastRP->nblocks = fMap.result.nblocks;
				if ( (ourSuper->verbose&VERBOSE_WRITES) )
					fprintf(ourSuper->logFile, "%s: allocateRPSectors('%s'): copy %d RP %d extended to 0x%X/0x%X\n",
							title, inode->fileName, altIdx, rpIdx-1, lastRP->start, lastRP->nblocks);
			}
			else
			{
				/* A non-contiguous chunk needs its own retrieval pointer. */
				if ( rpIdx >= FSYS_MAX_FHPTRS )
				{
					fprintf(ourSuper->logFile,"%s: allocateRPSectors(): file '%s' copy %d needs more than %ld retrieval pointers. Returned ENOSPC\n",
							title, inode->fileName, altIdx, (long)FSYS_MAX_FHPTRS);
					mgwfsFreeSectors(ourSuper, &fMap.result, 0);	/* don't leak the chunk */
					return -ENOSPC;
				}
				rps[rpIdx].start = fMap.result.start;
				rps[rpIdx].nblocks = fMap.result.nblocks;
				added = fMap.result.nblocks;
				if ( (ourSuper->verbose&VERBOSE_WRITES) )
					fprintf(ourSuper->logFile, "%s: allocateRPSectors('%s'): copy %d added RP %d 0x%X/0x%X\n",
							title, inode->fileName, altIdx, rpIdx, rps[rpIdx].start, rps[rpIdx].nblocks);
				++rpIdx;
			}
			if ( added <= 0 )		/* defensive: no forward progress -> bail */
			{
				fprintf(ourSuper->logFile,"%s: allocateRPSectors(): file '%s' made no progress (copy %d at %d/%d). Returned ENOSPC\n",
						title, inode->fileName, altIdx, have, sectors);
				return -ENOSPC;
			}
			have += added;
		}
	}
	addToDirty("allocRPSectors()", ourSuper,FSYS_INDEX_FREE);
	inode->fsHeader.clusters = sectors;
	return 0;
}

static int writeWholeFile(const char *title,  MgwfsSuper_t *ourSuper, MgwfsInode_t *inode)
{
	off64_t sector;
	int needFH, ptrIdx=0, retSize=0, blkLimit;
	ssize_t limit, wrSts;
	FsysRetPtr *retPtr;
	int bytes, copyCnt, copies;
	uint32_t sectors;
	IndexSys_t *fhLBA;
	RwBuff_t *rwBuff;
	
	rwBuff = &inode->rwb;
	sectors = (rwBuff->buffUsed + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR;
	bytes = sectors*BYTES_PER_SECTOR;
	fhLBA = ourSuper->indexSys + inode->inode_no;
	/* Decide whether the inode still needs header sectors from its own canonical
	 * copy (inode->fhSectors), not from the indexSys[] serialization buffer: a
	 * freshly created inode has zeroed fhSectors even though its indexSys[] slot
	 * may still hold stale LBAs from a previous occupant. Allocating fresh
	 * sectors both makes the inode persist correctly and avoids scribbling its
	 * header over whatever used to live at the stale location. */
	/* inode 0 is index.sys itself; its header lives at fixed locations recorded
	 * in the home block (writeFileHeader handles that), so it must never be
	 * given freshly allocated header sectors. Doing so would relocate index.sys
	 * without updating the home block, and the old copy would be read on the
	 * next mount. */
	needFH = inode->inode_no && (!inode->fhSectors[0] || (inode->fhSectors[0] & FSYS_EMPTYLBA_BIT));
	if ( !needFH )
	{
		int ii;
		copies = 0;
		for (ii=0; ii < FSYS_MAX_ALTS; ++ii)
		{
			if ( !inode->fsHeader.pointers[ii][0].nblocks )
				break;
			++copies;
		}
		if ( !copies )
			copies = 1;
	}
	else
		copies = ourSuper->defaultCopies;
	if ( (ourSuper->verbose & VERBOSE_WRITES) )
	{
		fprintf(ourSuper->logFile,"%s: writeWholeFile('%s'), inode_no=%d, rwBuff=%p, rwBuffUsed=%d, rwBuffOffset=0x%lX, rwBuffSize=%d, sectors=%d, clusters=%d, bytes=%d, copies=%d, needFH=%d\n"
				,title
				,inode->fileName
				,inode->inode_no
				,rwBuff->buff
				,rwBuff->buffUsed
				,rwBuff->buffOffset
				,rwBuff->buffSize
				,sectors
				,inode->fsHeader.clusters
				,bytes
				,copies
				,needFH
				);
	}
	if ( needFH)
	{
		// Need to allocate file header sectors
		if ( allocateFHSectors(ourSuper,inode,fhLBA) < 0 )
		{
			fprintf(ourSuper->logFile,"writeWholeFile() returned from allocateFHSectors() with -1. Quit with -ENOSPC\n");
			return -ENOSPC;
		}
	}
	if ( sectors > inode->fsHeader.clusters )
	{
//		int newBuffSize;
		// Need to add more sectors to file
		if ( allocateRPSectors("writeWholeFile()", ourSuper, inode, rwBuff, sectors) < 0 )
		{
			fprintf(ourSuper->logFile,"writeWholeFile() returned from allocateRPSectors() with -1. Quit with -ENOSPC\n");
			return -ENOSPC;
		}
//		fprintf(ourSuper->logFile, "writeWholeFile() returned from allocateRPSectors() with >=0. clusters=%d\n", inode->fsHeader.clusters);
#if 0
		inode->fsHeader.clusters += ourSuper->homeBlk.def_extend;
		newBuffSize = inode->fsHeader.clusters * BYTES_PER_SECTOR;
		if ( rwBuff->buffSize < newBuffSize )
		{
			uint8_t *newBuff;
			newBuff = (uint8_t *)realloc(rwBuff->buff, newBuffSize );
			if ( !newBuff )
			{
				fprintf(ourSuper->logFile, "%s: fileExtend('%s'). Out of memory. Failed to realloc(%d) bytes\n",
						title,
						inode->fileName,
						newBuffSize);
				if ( rwBuff->buff )
					free(rwBuff->buff);
				rwBuff->buffErr = -ENOMEM;
				rwBuff->buff = NULL;
				rwBuff->buffOffset = 0;
				rwBuff->buffSize = 0;
				return -ENOMEM;
			}
			rwBuff->buff = newBuff;
			rwBuff->buffSize = newBuffSize;
		}
#endif
	}
	/* write the file to disk */
	for (copyCnt=0; copyCnt < copies; ++copyCnt)
	{
		retPtr = inode->fsHeader.pointers[copyCnt] + 0;
		ptrIdx = 0;
		retSize = 0;
#if 0
		{
			int jj,len;
			char buf[256];
			len = snprintf(buf,sizeof(buf),"%s writeWholeFile(): RPs [0]:", title);
			for (jj=0; jj < FSYS_MAX_FHPTRS; ++jj)
			{
				len += snprintf(buf + len, sizeof(buf) - len, " 0x%X/0x%X",
								inode->fsHeader.pointers[copyCnt][jj].start,
								inode->fsHeader.pointers[copyCnt][jj].nblocks
								);
				if ( jj > 0 && !inode->fsHeader.pointers[copyCnt][jj].nblocks )
					break;
			}
			fprintf(ourSuper->logFile,"%s\n",buf);
		}
		fprintf(ourSuper->logFile,"%s writeWholeFile(): Before: sector=%ld, blkLimit=%d, limit=%ld, retPtr=%p, ptrIdx=%d\n"
				,title
				,sector
				,blkLimit
				,limit
				,retPtr
				,ptrIdx
				);
#endif
		while ( retSize < bytes )
		{
			sector = retPtr->start;
			blkLimit = retPtr->nblocks;
			limit = bytes - retSize;
#if 0
			fprintf(ourSuper->logFile,"%s writeWholeFile(): Before: sector=%ld, blkLimit=%d, limit=%ld, retPtr=%p, ptrIdx=%d\n"
					,title
					,sector
					,blkLimit
					,limit
					,retPtr
					,ptrIdx
					);
#endif
			if ( blkLimit*BYTES_PER_SECTOR > limit )
				blkLimit = ((blkLimit*BYTES_PER_SECTOR-limit)+BYTES_PER_SECTOR-1)/BYTES_PER_SECTOR;
            if ( limit > retPtr->nblocks*BYTES_PER_SECTOR )
            {
                limit = retPtr->nblocks*BYTES_PER_SECTOR;
                ++retPtr;   /* advance to next RP */
                ++ptrIdx;
            }
#if 0
			fprintf(ourSuper->logFile,"%s writeWholeFile(): After: sector=%ld, blkLimit=%d, limit=%ld, retPtr=%p, ptrIdx=%d\n"
					,title
					,sector
					,blkLimit
					,limit
					,retPtr
					,ptrIdx
					);
#endif
			if ( (ourSuper->verbose&VERBOSE_WRITES) )
			{
				fprintf(ourSuper->logFile,"%s: Writing %ld bytes (%ld sectors) at sector 0x%08lX for copy %d of %s\n",
						title, limit, limit/BYTES_PER_SECTOR, sector, copyCnt, inode->fileName);
			}
            if ( lseek64(ourSuper->fd,(sector+ourSuper->baseSector)*BYTES_PER_SECTOR,SEEK_SET) == (off64_t)-1 )
            {
                fprintf(ourSuper->errFile, "%s: Failed to seek to sector 0x%lX on %s: %s\n", title, sector, inode->fileName, strerror(errno));
                return -EIO;
            }
			wrSts = write(ourSuper->fd, rwBuff->buff+retSize, limit);
			if ( wrSts != limit )
			{
				fprintf(ourSuper->errFile,"%s: Failed to write %ld bytes to %s. Instead got %ld: %s\n",
						title, limit, inode->fileName, wrSts, strerror(errno));
				return -EIO;
			}
			retSize += limit;
		}
	}
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

static uint8_t *insertFilenameIntoDir(uint8_t *ptr, int inodeIdx, const char *fileName)
{
	int fnLen = strlen(fileName);
	*ptr++ = inodeIdx&0xFF;
	*ptr++ = (inodeIdx>>8)&0xFF;
	*ptr++ = (inodeIdx>>16)&0xFF;
	*ptr++ = 1;	// Generation number is always 1 for now
	*ptr++ = fnLen+1;
	memcpy(ptr,fileName,fnLen);
	ptr += fnLen;
	*ptr++ = 0;
	return ptr;
}

int updateAllMetaData(const char *title, MgwfsSuper_t *ourSuper)
{
	int sts=0;
	int inodeIdx, ii;

	LOCK_IT("wrMutex",ourSuper,&wrMutex);
	while( (inodeIdx = popFmDirty(ourSuper)) >= 0)
	{
		MgwfsInode_t *inode, **iPtr;
		IndexSys_t *fhLBAs;
		
		inode = ourSuper->inodeList[inodeIdx];
		/* The slot may have been freed after it was marked dirty (e.g. rmdir
		 * removes a directory that an earlier child-removal had already flagged).
		 * Its now-empty entry is persisted when index.sys is rebuilt below, so
		 * there's nothing to write back here; skip it rather than deref NULL. */
		if ( !inode )
			continue;
		switch (inodeIdx)
		{
		case FSYS_INDEX_INDEX:
			if ( inode->rwb.buff )
				free(inode->rwb.buff);
			inode->rwb.buff = (uint8_t *)calloc(ourSuper->numInodesAvailable, sizeof(uint32_t) * FSYS_MAX_ALTS);
			iPtr = ourSuper->inodeList;
			fhLBAs = (IndexSys_t *)inode->rwb.buff;
			for (ii=0; ii < ourSuper->numInodesUsed; ++ii)
			{
				inode = *iPtr++;
				if ( !inode )
					fhLBAs->lba[0] = FSYS_EMPTYLBA_BIT;
				else
					memcpy(fhLBAs, inode->fhSectors, sizeof(IndexSys_t));
				++fhLBAs;
			}
			inode = ourSuper->inodeList[inodeIdx];
			inode->rwb.buffSize = ourSuper->numInodesAvailable * (sizeof(IndexSys_t));
			inode->rwb.buffUsed = ii * sizeof(IndexSys_t);
			inode->rwb.buffOffset = inode->rwb.buffUsed;
			/* The index grew/shrank with numInodesUsed, so its header size must
			 * track it; otherwise readWholeFile() reads a stale length on the
			 * next mount and silently drops the newest inode entries. */
			inode->fsHeader.size = inode->rwb.buffUsed;
			break;
		case FSYS_INDEX_FREE:
			inode->rwb.buff = ourSuper->freeMap.rwBuff.buff;
			inode->rwb.buffSize = inode->fsHeader.clusters * FSYS_CLUSTER_SIZE;
			inode->rwb.buffOffset = inode->fsHeader.size;
			inode->rwb.buffUsed = inode->rwb.buffOffset;
			break;
		default:
			/* Only derive the size from the read/write buffer when that buffer
			 * actually holds the file's data. After a data flush the buffer is
			 * freed and zeroed (see end of this loop), so a later metadata-only
			 * re-dirty -- e.g. utimens or rename after the file was released --
			 * would otherwise reset fsHeader.size to 0 and the next mount would
			 * read the file back as empty. With no live buffer the previously
			 * persisted size already on the header is the truth; leave it. */
			if ( inode->rwb.buff )
				inode->fsHeader.size = inode->rwb.buffUsed;
			break;
		}
		/* Don't restamp if utimens (or similar) already set an explicit
		 * mtime; just consume the flag so a later data write stamps "now"
		 * again. */
		if ( (inode->flags & MGWFS_INODE_MTIME_SET) )
			inode->flags &= ~MGWFS_INODE_MTIME_SET;
		else
			inode->fsHeader.mtime = time(NULL);
		if ( !inode->rwb.buff && inode->fsHeader.type == FSYS_TYPE_DIR )
		{
			/* Pack the directory contents into rwb.buff for write-back. This
			 * covers empty directories too (idxChildTop == 0), e.g. a freshly
			 * created one, so their "." / ".." entries get laid down on media. */
			sts = writeDirectory(ourSuper, inode);
			if ( sts < 0 )
				break;
		}
		if ( inode->rwb.buff )
		{
			UNLOCK_IT("wrMutex", ourSuper, &wrMutex);
			sts = writeWholeFile("updateAllMetaData()", ourSuper, inode);
			LOCK_IT("wrMutex", ourSuper, &wrMutex);
		}
		if ( inodeIdx != FSYS_INDEX_FREE && inode->rwb.buff )
			free(inode->rwb.buff);
		memset(&inode->rwb,0,sizeof(inode->rwb));
		if ( sts < 0 )
			break;
		sts = writeFileHeader(ourSuper,inode);
		if ( (ourSuper->verbose&VERBOSE_WRITES) )
			fflush(ourSuper->logFile);
	}
	fprintf(ourSuper->logFile,"Exiting updateAllMetaData(): sts=%d\n", sts);
	fflush(ourSuper->logFile);
	UNLOCK_IT("wrMutex", ourSuper, &wrMutex);
	return sts;
}

int fileClose(const char *title, MgwfsSuper_t *ourSuper, FuseFH_t *fhp)
{
	int sts=0;
	if ( (fhp->openFlags&(O_RDWR|O_WRONLY)) )
	{
		MgwfsInode_t *inode = ourSuper->inodeList[fhp->inode];
		addToDirty("fileClose()", ourSuper, inode->inode_no);
		sts = updateAllMetaData(title,ourSuper);
	}
	return sts;
}

int fileRename(const char *title, MgwfsSuper_t *ourSuper, const char *oldPath, const char *newPath)
{
	return -EIO;
}

static void markInodeUnused(MgwfsSuper_t *ourSuper, MgwfsInode_t **inodePtr)
{
	MgwfsInode_t *inode = *inodePtr;
	int idx = inode->inode_no;
	RwBuff_t *rwb = &inode->rwb;
	if ( rwb->buff )
		free(rwb->buff);
	memset(rwb,0,sizeof(RwBuff_t));
	free(inode);
	ourSuper->inodeList[idx] = NULL;
	*inodePtr = NULL;
}

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
	}
	else
	{
		if ( (ourSuper->verbose&(VERBOSE_WRITES)) )
			fprintf(ourSuper->logFile, "findUnusedInode(): Reused inode %d.\n", idx);
	}
	/* If we are claiming a brand new slot at the end (rather than reusing a
	 * previously freed one), the used count has to grow to include it; the
	 * index.sys writer only persists entries [0, numInodesUsed). */
	if ( idx >= ourSuper->numInodesUsed )
		ourSuper->numInodesUsed = idx + 1;
	inode = (MgwfsInode_t *)calloc(1,sizeof(MgwfsInode_t));
	inode->inode_no = idx;
	inode->fsHeader.ctime = time(NULL);
	inode->fsHeader.id = FSYS_ID_HEADER;
	/* insertFilenameIntoDir() stamps directory entries with generation 1, so
	 * the header has to carry the same generation or the read path will reject
	 * the entry ("bad generation") when the volume is next mounted. */
	inode->fsHeader.generation = 1;
	ourSuper->inodeList[idx] = inode;
	addToDirty("findUnusedInode():", ourSuper, FSYS_INDEX_INDEX);
	return inode;
}

int fileExtend(const char *title, MgwfsSuper_t *ourSuper, FuseFH_t *fhp)
{
	MgwfsInode_t *inode;
	int newBuffSize;
	RwBuff_t *rwBuff;
	
	inode = ourSuper->inodeList[fhp->inode];
	inode->fsHeader.clusters += ourSuper->homeBlk.def_extend;
	newBuffSize = inode->fsHeader.clusters * BYTES_PER_SECTOR;
	rwBuff = &inode->rwb;
	if ( rwBuff->buffSize < newBuffSize )
	{
		uint8_t *newBuff;
		newBuff = (uint8_t *)realloc(rwBuff->buff, newBuffSize );
		if ( !newBuff )
		{
			fprintf(ourSuper->logFile, "%s: fileExtend('%s'). Out of memory. Failed to realloc(%d) bytes\n",
					title,
					inode->fileName,
					newBuffSize);
			if ( rwBuff->buff )
				free(rwBuff->buff);
			rwBuff->buffErr = -ENOMEM;
			rwBuff->buff = NULL;
			rwBuff->buffOffset = 0;
			rwBuff->buffSize = 0;
			return -ENOMEM;
		}
		rwBuff->buff = newBuff;
		rwBuff->buffSize = newBuffSize;
	}
	return 0;
}

static int insertIntoDir(MgwfsSuper_t *ourSuper, MgwfsInode_t *dirInode, MgwfsInode_t *fileInode)
{
	MgwfsInode_t *inode;
	int idx;
	
	fileInode->idxChildTop = 0;
	fileInode->idxParentInode = dirInode->inode_no;
	fileInode->idxPrevInode = 0;
	idx = dirInode->idxChildTop;
	fileInode->idxNextInode = idx;
	if ( idx )
	{
		inode = ourSuper->inodeList[idx];
		if ( inode )
			inode->idxPrevInode = fileInode->inode_no;
	}
	dirInode->idxChildTop = fileInode->inode_no;
	/* The directory's contents changed, so it must be re-packed and written. */
	addToDirty("insertIntoDir():", ourSuper, dirInode->inode_no);
	return 0;
}

int fileCreate(const char *title, const char *path,  MgwfsSuper_t *ourSuper)
{
	int sts, sLen;
	MgwfsInode_t *dirInode, *inode;
	char *dir;
	char *tmpDir=NULL;
	const char  *nameOnly;
	
	inode = findUnusedInode(ourSuper);
	if ( !inode )
	{
		fprintf(ourSuper->logFile, "%s: fileCreate(%s) failed to find empty inode, returned ENOSPC\n", title, path);
		return -ENOSPC;
	}
	if ( inode->rwb.buff )
		free(inode->rwb.buff);
	memset(&inode->rwb,0,sizeof(inode->rwb));
	sLen = strlen(path) + 2;
	tmpDir = (char *)malloc(sLen);
	strncpy(tmpDir,path,sLen);
	nameOnly = strrchr(path,'/');
	if ( !nameOnly )
		nameOnly = path;
	else
		++nameOnly;					/* step past the '/' to the bare filename */
	strncpy(inode->fileName,nameOnly,sizeof(inode->fileName)-1);
	inode->fileName[sizeof(inode->fileName)-1] = 0;
	/* A freshly allocated header is FSYS_TYPE_EMPTY (0); mark it a plain file
	 * so it both reads back as a regular file (main.c derives st_mode from
	 * this) and isn't mistaken for an unused header. */
	inode->fsHeader.type = FSYS_TYPE_FILE;
	inode->mode = S_IFREG | 0664;
	dir = strrchr(tmpDir, '/');
	if ( !dir || dir == tmpDir )
	{
		tmpDir[0] = '/';
		tmpDir[1] = 0;
	}
	else 
		*dir = 0;
	dirInode = NULL;
	sts = findInode(ourSuper, FSYS_INDEX_ROOT, tmpDir);
	if ( sts > 0 )
	{
		dirInode = ourSuper->inodeList[sts];
		if ( !dirInode )
			sts = -ENOTDIR;
		else
			sts = 0;
	}
	else
		sts = -ENOTDIR;
	if ( dirInode && S_ISDIR(dirInode->mode) )
	{
//		fprintf(ourSuper->logFile,"Before insert:\n");
//		tree(ourSuper, FSYS_INDEX_ROOT, 0 );
		sts = insertIntoDir(ourSuper, dirInode, inode);
//		fprintf(ourSuper->logFile,"After insert:\n");
//		tree(ourSuper, FSYS_INDEX_ROOT, 0 );
	}
	if ( sts )
	{
		fprintf(ourSuper->logFile,"%s: fileCreate('%s') call to insertIntoDir() returned error %d\n", title, path, sts);
		markInodeUnused(ourSuper,&inode);
	}
	else
	{
		sts = inode->inode_no;
	}
	fflush(ourSuper->logFile);
	free(tmpDir);
	if ( (ourSuper->verbose&(VERBOSE_WRITES)) )
	{
		fprintf(ourSuper->logFile, "%s: fileCreate(%s). Added %d, dirInode=%d, idxChildTop=%d, returned sts=%d\n"
				,title
				,path
				,inode ? inode->inode_no : 0
				,dirInode ? dirInode->inode_no : 0
				,dirInode ? dirInode->idxChildTop : 0
				,sts
				);
	}
	return sts;
}

int fileWrite(const char *title, MgwfsSuper_t *ourSuper, FuseFH_t *fhp, off_t offset, size_t bytes)
{
	return -EIO;
}

void dumpIndex(FILE *outp, IndexSys_t *indexBase, int bytes)
{
	int ii=0;
	IndexSys_t *index = indexBase;
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
		fprintf(outp, "    %5d: 0x%08X 0x%08X 0x%08X", ii, index->lba[0], index->lba[1], index->lba[2]);
		if ( ii < 4 )
			fprintf(outp, " (%s)", Titles[ii]);
		fprintf(outp, "\n");
		++ii;
		++index;
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

void dumpDir(FILE *outp, uint8_t *dirBase, int bytes, MgwfsSuper_t *ourSuper, IndexSys_t *indexSys )
{
	uint8_t *dir = dirBase;
	int fd=0, ii=0;
	FsysHeader hdr;
	
	if ( indexSys )
	{
		fprintf(outp, "Contents of rootdir.sys:\n");
		fd = ourSuper->fd;
	}
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
			if ( getFileHeader((char *)dir, ourSuper, FSYS_ID_HEADER, indexSys + fid, &hdr) )
				displayFileHeader(ourSuper->logFile,&hdr,1|(ourSuper->verbose&VERBOSE_RETPTRS));
		}
		++ii;
		dir += txtLen;
	}
}

void verifyFreemap(MgwfsSuper_t *ourSuper)
{
	int idx, altIdx;
	int verbSave = ourSuper->verbose;
	MgwfsSuper_t tmpSuper;
	FreeMap_t *tmpFreeMap = &tmpSuper.freeMap, *freeMap = &ourSuper->freeMap;
	FsysRetPtr *ourUsedMap, *rp, *rpMax;
	uint32_t mlstrt;
	IndexSys_t *indexPtr;
	MgwfsInode_t *inodePtr, **inodePtrPtr;
	FsysRetPtr tmp, *missingList, *mlptr;
	int missingListSize;
	uint32_t totalFreeSectors, totalUsedSectors, totalMergedSectors, totalFakeFree=0;
	
	/* Actually a cheat. We're using the freemap routines to
	   build a 'used' list instead of a freelist
	*/
	fprintf(ourSuper->logFile, "Total entries available in free map: %d, Used in free map: %d\n", freeMap->freeMapEntriesAvail, freeMap->freeMapEntriesUsed);
	fprintf(ourSuper->logFile, "Total sectors free %d, used %d, lost %d\n", freeMap->sectorsFree, freeMap->sectorsUsed, freeMap->sectorsLost);
	idx = dumpFreemap(ourSuper->logFile, "Contents of freemap before merge:", (FsysRetPtr *)freeMap->rwBuff.buff, freeMap->freeMapEntriesAvail, &totalFreeSectors);
	fprintf(ourSuper->logFile,"Total free sectors: %d, idx=%d, freeMapUsed=%d %s\n",
			totalFreeSectors,
			idx,
			freeMap->freeMapEntriesUsed,
			idx==freeMap->freeMapEntriesUsed ? "MATCH":" ***DIFFERENT*** ");
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
	tmpSuper.inodeList[FSYS_INDEX_FREE]->fsHeader.clusters = 10;
	tmpFreeMap->freeMapEntriesAvail = 2*(tmpSuper.inodeList[FSYS_INDEX_FREE]->fsHeader.clusters*BYTES_PER_SECTOR)/sizeof(FsysRetPtr);
	ourUsedMap = (FsysRetPtr *)calloc(tmpFreeMap->freeMapEntriesAvail,sizeof(FsysRetPtr));
	tmpFreeMap->rwBuff.buff = (uint8_t *)ourUsedMap;
	tmpFreeMap->freeMapEntriesUsed = 0;
	tmp.nblocks = 1;
	/* Prepopulate the "used" list with the home block usage */
	for (idx=0; idx < FSYS_MAX_ALTS; ++idx)
	{
		if ( (tmp.start = ourSuper->homeLbas[idx]) )
			mgwfsFreeSectors(&tmpSuper,&tmp,FALSE);
	}
	/* Populate the rest of the used list by reading all the file headers */
	indexPtr = ourSuper->indexSys;
	for (idx=0; idx < ourSuper->indexSysHdr.size/sizeof(uint32_t); idx += FSYS_MAX_ALTS, ++indexPtr)
	{
		if ( (indexPtr->lba[0] & FSYS_LBA_MASK) && !(indexPtr->lba[0] & FSYS_EMPTYLBA_BIT) )
		{
			/* Add all the sectors of the file's header */
			for (altIdx=0; altIdx < FSYS_MAX_ALTS; ++altIdx)
			{
				tmp.start = indexPtr->lba[altIdx];
				mgwfsFreeSectors(&tmpSuper,&tmp,FALSE);
			}
		}
	}
	inodePtrPtr = ourSuper->inodeList;
	for ( idx = 0; idx < ourSuper->numInodesAvailable; ++idx )
	{
		if ( !(inodePtr = *inodePtrPtr++) )
			break;
		for (altIdx=0; altIdx < FSYS_MAX_ALTS; ++altIdx)
		{
			/* Add all the sectors of the file's contents */
			rp = inodePtr->fsHeader.pointers[altIdx];
			rpMax = rp+FSYS_MAX_FHPTRS;
			while ( rp < rpMax && rp->nblocks && rp->start )
			{
				mgwfsFreeSectors(&tmpSuper,rp,FALSE);
				++rp;
			}
		}
	}
	ourSuper->verbose = verbSave;
	idx = dumpFreemap(ourSuper->logFile, "Contents of \"used\" blocks before merge:", ourUsedMap, tmpFreeMap->freeMapEntriesAvail, &totalUsedSectors);
	fprintf(ourSuper->logFile,"Total used sectors: %d, idx=%d, freeMapUsed=%d %s\n",
			totalUsedSectors,
			idx,
			tmpFreeMap->freeMapEntriesUsed,
			idx==tmpFreeMap->freeMapEntriesUsed ? "MATCH":" ***DIFFERENT*** ");
	fprintf(ourSuper->logFile,"Total of free+used: %d. Total missing: %d\n",
			totalFreeSectors+totalUsedSectors,
			ourSuper->homeBlk.max_lba-1-(totalFreeSectors+totalUsedSectors)
			);
	rp = ourUsedMap;
	rpMax = ourUsedMap + tmpFreeMap->freeMapEntriesAvail;
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
	tmpFreeMap->freeMapEntriesAvail = freeMap->freeMapEntriesAvail+idx;
	tmpFreeMap->rwBuff.buff = (uint8_t *)malloc(freeMap->freeMapEntriesAvail * sizeof(FsysRetPtr));
	memcpy(tmpSuper.freeMap.rwBuff.buff, freeMap->rwBuff.buff, freeMap->freeMapEntriesAvail * sizeof(FsysRetPtr));
	tmpFreeMap->freeMapEntriesUsed = freeMap->freeMapEntriesUsed;
	/* Copy the actual freemap.sys fileheader to local */
	tmpSuper.inodeList[FSYS_INDEX_FREE]->fsHeader = ourSuper->inodeList[FSYS_INDEX_FREE]->fsHeader;
	/* ourUsedMap holds list of used sectors */
	fprintf(ourSuper->logFile, "Total of used entries list: %d, total of free entries: %d, total of potential both used+free: %d, total available: %d\n",
			idx,
			tmpFreeMap->freeMapEntriesUsed,
			idx + tmpFreeMap->freeMapEntriesUsed,
			tmpFreeMap->freeMapEntriesAvail);
	rp = ourUsedMap;
	rpMax = ourUsedMap + tmpFreeMap->freeMapEntriesAvail;
	/* Walk the list of used sectors and free them */
	while ( rp < rpMax && rp->start && rp->nblocks )
	{
		mgwfsFreeSectors(&tmpSuper,rp,FALSE);
		++rp;
	}
	idx = rp-ourUsedMap;
	fprintf(ourSuper->logFile, "Free'd a total of %d used entries\nThe following should have just one entry from 0x01 to 0x%08X\n",
			idx,
			ourSuper->homeBlk.max_lba - 1);
	idx = dumpFreemap(ourSuper->logFile, "Contents of freemap.sys after merge:", (FsysRetPtr *)tmpFreeMap->rwBuff.buff, tmpFreeMap->freeMapEntriesAvail, &totalMergedSectors);
	fprintf(ourSuper->logFile,"Total merged sectors: %d, idx=%d, freeMapUsed=%d %s\n",
			totalFreeSectors,
			idx,
			tmpFreeMap->freeMapEntriesUsed,
			idx==tmpFreeMap->freeMapEntriesUsed ? "MATCH":" ***DIFFERENT*** ");
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
	free(tmpFreeMap->rwBuff.buff);
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
	if ( readWholeFile(inode->fileName, ourSuper, dirContents, inode->fsHeader.size, inode->fsHeader.pointers[0] ) < 0 )
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
		/* If the index is outside the limit provided for in the index.sys file,
		 * or it names a slot that holds no loaded inode (e.g. a stale directory
		 * entry left pointing at a since-freed inode), it's nfg. The NULL check
		 * guards the dereference below. */
		if ( fid >= ourSuper->numInodesAvailable || !ourSuper->inodeList[fid] )
		{
			fprintf(ourSuper->logFile, "%sFound file '%s' in dir '%s' with invalid/unallocated fid: %d. fid must be 0 < fid < %d and resolve to a live inode. Skipped\n",
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
	if ( nest >= 20 )
	{
		fprintf(stderr,"tree(): Called with topIdx of %d and nest of %d too deep\n", topIdx, nest);
		return -1;
	}
	if ( !nest )
		fprintf(ourSuper->logFile, " Num Nxt Prv Chd\n");
	do
	{
		fprintf(ourSuper->logFile
				,"%4d%4d%4d%4d%*s%s%s\n"
				,inode->inode_no
				,inode->idxNextInode
				,inode->idxPrevInode
				, inode->idxChildTop
				,nest
				," "
				,inode->fileName
				,S_ISDIR(inode->mode) ? "/" : ""
				);
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
		for (idx=0; idx < ourSuper->numFuseFHs; ++idx, ++fhp)
		{
			if ( !fhp->index )
			{
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
		if ( !(fhp->openFlags&(O_WRONLY|O_RDWR)) )
		{
			MgwfsInode_t *inode = ourSuper->inodeList[fhp->inode];
			if ( inode->rwb.buff )
				free(inode->rwb.buff);
			memset(&inode->rwb,0,sizeof(inode->rwb));
		}
		memset(fhp, 0, sizeof(FuseFH_t));
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

/*
 * Write an inode's FsysHeader (which carries size, clusters, ctime, mtime and
 * the retrieval pointers) out to every alternate sector that holds a copy of
 * it. This is the inverse of getFileHeader(): there is no checksum in the
 * header, so the read path validates by id and by comparing the alternates,
 * which means we just have to lay down an identical copy in each alternate.
 *
 * Returns 0 if at least one alternate was written, -EIO if every alternate
 * failed (consistent with the read path, which is happy as long as any one
 * good copy survives).
 */
int writeFileHeader(MgwfsSuper_t *super, MgwfsInode_t *inode)
{
	int fd, alts, wrote=0;
	uint32_t fileID, sector;
	IndexSys_t *lbas;

	if ( inode->inode_no )
	{
		/* An ordinary file/directory header; its alternate LBAs live in the
		 * index.sys entry for this inode. */
		fileID = FSYS_ID_HEADER;
		lbas = super->indexSys + inode->inode_no;
	}
	else
	{
		/* The index.sys file itself; its alternate LBAs live in the home block. */
		fileID = FSYS_ID_INDEX;
		lbas = (IndexSys_t *)super->homeBlk.index;
	}
	inode->fsHeader.id = fileID;
	fd = super->fd;
	for (alts=0; alts < FSYS_MAX_ALTS; ++alts)
	{
		ssize_t sts;

		/* An empty/absent alternate has no sector to write to. */
		if ( !lbas->lba[alts] || (lbas->lba[alts] & FSYS_EMPTYLBA_BIT) )
			continue;
		sector = lbas->lba[alts] + super->baseSector;
		if ( (super->verbose&VERBOSE_WRITES) )
		{
			fprintf(super->logFile, "writeFileHeader(): Writing file header for '%s' (inode %d) at sector 0x%X\n",
					inode->fileName, inode->inode_no, sector);
		}
		if ( lseek64(fd, (off64_t)sector*BYTES_PER_SECTOR, SEEK_SET) == (off64_t)-1 )
		{
			fprintf(super->errFile, "writeFileHeader(): Failed to lseek to sector 0x%X for '%s': %s\n",
					sector, inode->fileName, strerror(errno));
			continue;
		}
		sts = write(fd, (uint8_t *)&inode->fsHeader, sizeof(FsysHeader));
		if ( sts != sizeof(FsysHeader) )
		{
			fprintf(super->errFile, "writeFileHeader(): Failed to write %ld byte file header of '%s' at sector 0x%X: %s\n",
					sizeof(FsysHeader), inode->fileName, sector, strerror(errno));
			continue;
		}
		++wrote;
	}
	if ( !wrote )
	{
		fprintf(super->errFile, "writeFileHeader(): Failed to write any copy of the file header for '%s' (inode %d)\n",
				inode->fileName, inode->inode_no);
		return -EIO;
	}
	return 0;
}

/*
 * Materialize a directory's entries into dir->rwb.buff in the on-disk format
 * (see insertFilenameIntoDir()): a synthesized ".." and "." followed by one
 * entry per child inode. The caller (updateAllMetaData) then hands rwb.buff to
 * writeWholeFile() to lay it onto media, so this routine only builds the
 * buffer; it does not perform the disk write itself.
 *
 * Returns 0 on success, -ENOMEM if the buffer can't be allocated.
 */
int writeDirectory(MgwfsSuper_t *super, MgwfsInode_t *dir)
{
	MgwfsInode_t *top, *child;
	int nxt, siz, alloc;
	uint8_t *ptr;

	/* Size the buffer: the two synthesized entries (".." and ".") plus one
	 * entry per child. Each on-disk entry (see insertFilenameIntoDir()) is a
	 * 5 byte fixed part -- 3 byte inode index, 1 byte generation, 1 byte name
	 * length -- followed by the name and a trailing NUL, i.e. 5 + namelen + 1.
	 * Counting the fixed part as a single 4 byte word here under-sized every
	 * entry by one byte; near a sector boundary the packed contents then ran
	 * past the rounded-up allocation and corrupted the heap. */
#define DIR_ENTRY_BYTES(namelen) (5 + (namelen) + 1)
	siz = DIR_ENTRY_BYTES(2) + DIR_ENTRY_BYTES(1);	/* ".." and "." */
	nxt = dir->idxChildTop;
	top = super->inodeList[dir->inode_no];
	/* An empty directory has no children (idxChildTop == 0); it still gets the
	 * two synthesized ".." / "." entries sized above, so just skip the walk. */
	while ( nxt )
	{
		child = super->inodeList[nxt];
		siz += DIR_ENTRY_BYTES(strlen(child->fileName));
		nxt = child->idxNextInode;
	}
#undef DIR_ENTRY_BYTES
	/* writeWholeFile() writes whole sectors out of this buffer, so round the
	 * allocation up to a sector and zero it; the slack becomes clean padding
	 * rather than an over-read of adjacent heap. */
	alloc = ((siz + BYTES_PER_SECTOR-1)/BYTES_PER_SECTOR)*BYTES_PER_SECTOR;
	dir->rwb.buff = (uint8_t *)calloc(1, alloc);
	if ( !dir->rwb.buff )
	{
		fprintf(super->errFile, "writeDirectory(): Out of memory allocating %d bytes for directory '%s' (inode %d)\n",
				alloc, top->fileName, dir->inode_no);
		dir->rwb.buffSize = dir->rwb.buffUsed = dir->rwb.buffOffset = 0;
		return -ENOMEM;
	}
	dir->rwb.buffSize = alloc;
	ptr = dir->rwb.buff;
	ptr = insertFilenameIntoDir(ptr, dir->idxParentInode, "..");
	ptr = insertFilenameIntoDir(ptr, dir->inode_no, ".");
	nxt = dir->idxChildTop;
	while ( nxt )
	{
		child = super->inodeList[nxt];
		ptr = insertFilenameIntoDir(ptr, nxt, child->fileName);
		nxt = child->idxNextInode;
	}
	dir->rwb.buffUsed = ptr - dir->rwb.buff;
	dir->rwb.buffOffset = dir->rwb.buffUsed;
	/* The directory is packed after updateAllMetaData() ran its size = buffUsed
	 * step (which saw an empty buffer), so set the header size here from the
	 * freshly packed contents; otherwise the header persists size 0 and the
	 * directory reads back empty on the next mount. */
	dir->fsHeader.size = dir->rwb.buffUsed;
	if ( (super->verbose&VERBOSE_WRITES) )
	{
		int blk, rp;
		fprintf(super->logFile, "writeDirectory(): Updated directory at inode 0x%X: %s\n", dir->inode_no, top->fileName);
		dumpDir(super->logFile, dir->rwb.buff, ptr - dir->rwb.buff, super, NULL);
		fprintf(super->logFile,"RP's for directory:\n");
		for (blk=0; blk < FSYS_MAX_ALTS; ++blk)
		{
			fprintf(super->logFile,"   %d: ", blk);
			for (rp=0; rp < FSYS_MAX_FHPTRS; ++rp)
			{
				fprintf(super->logFile, " 0x%X/0x%X"
						,dir->fsHeader.pointers[blk][rp].start
						,dir->fsHeader.pointers[blk][rp].nblocks
						 );
				if ( !dir->fsHeader.pointers[blk][rp].start )
					break;
			}
			fprintf(super->logFile,"\n");
		}
		fflush(super->logFile);
	}
	return 0;
}

