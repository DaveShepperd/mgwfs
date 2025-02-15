/*
  mgwfsf: Atari/MidwayGamesWest filesystem using libfuse: Filesystem in Userspace

  Copyright (C) 2025  Dave Shepperd <mgwfsf@dshepperd.com>

  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.

 Build with enclosed Makefile

*/

#include "mgwfsf.h"

BootSector_t bootSect;
MgwfsSuper_t ourSuper;

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

int getHomeBlock(MgwfsSuper_t *ourSuper, uint32_t *lbas, off64_t maxHb, off64_t sizeInSectors, uint32_t *ckSumP)
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

int getFileHeader(const char *title, MgwfsSuper_t *ourSuper, uint32_t id, uint32_t lbas[FSYS_MAX_ALTS], FsysHeader *fhp)
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

void dumpIndex(uint32_t *indexBase, int bytes)
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

void dumpFreemap(const char *title, FsysRetPtr *rpBase, int entries )
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

void verifyFreemap(MgwfsSuper_t *ourSuper)
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

int unpackDir(MgwfsSuper_t *ourSuper, MgwfsInode_t *inode, int nest)
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

int tree(MgwfsSuper_t *ourSuper, int topIdx, int nest)
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
		inode = ourSuper->inodeList + inode->idxNextInode;
	} while (1);
	if ( ret && *path )
	{
		/* More stuff to look through. Though, this part has to be a directory */
		inode = ourSuper->inodeList + ret;
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
				fhp->index = idx + 1;
				fhp->inode = 0;
				fhp->instances = 0;
				fhp->offset = 0;
				if ( fhp->buffer )
				{
					free(fhp->buffer);
					fhp->buffer = NULL;
				}
				fhp->bufferSize = 0;
				return fhp;
			}
		}
	}
	idx = ourSuper->numFuseFHs;
	num = idx+64;
	fhp = (FuseFH_t *)realloc(ourSuper->fuseFHs,num*sizeof(FuseFH_t));
	ourSuper->fuseFHs = fhp;
	ourSuper->numFuseFHs = num;
	nFhp = fhp + idx;
	nFhp->index = idx+1;
	nFhp->inode = 0;
	nFhp->instances = 0;
	nFhp->offset = 0;
	if ( nFhp->buffer )
	{
		free(nFhp->buffer);
		nFhp->buffer = NULL;
	}
	nFhp->bufferSize = 0;
	nFhp->readAmt = 0;
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
		fhp->index = 0;
		fhp->inode = 0;
		fhp->instances = 0;
		fhp->offset = 0;
		if ( fhp->buffer )
		{
			free(fhp->buffer);
			fhp->buffer = NULL;
		}
		fhp->bufferSize = 0;
		fhp->readAmt = 0;
	}
}

