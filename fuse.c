/*
  mgwfs: Atari/MidwayGamesWest filesystem using libfuse: Filesystem in Userspace

  Copyright (C) 2025  Dave Shepperd <mgwfs@dshepperd.com>

  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.

 Build with enclosed Makefile

*/

#include "mgwfs.h"

#if !NO_MUTEXES
static pthread_mutex_t rdMutex = PTHREAD_MUTEX_INITIALIZER;

void fuse_destroy_mutex(void)
{
	pthread_mutex_destroy(&rdMutex);
}
#endif

static void *mgwfs_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_init()\n");
		fflush(ourSuper.logFile);
	}
	cfg->kernel_cache = 1;
	return NULL;
}

static int mgwfs_getattr(const char *path, struct stat *stbuf,
			 struct fuse_file_info *fi)
{
	int idx, ret=0;
	int wFlags = options.read_write ? 0220 : 0;
	MgwfsInode_t *inode;
	
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_getattr(path='%s',stbuf)\n", path);
		fflush(ourSuper.logFile);
	}
	memset(stbuf, 0, sizeof(struct stat));
	LOCK_IT("rdMutex",&ourSuper,&rdMutex);
	if ( (idx = findInode(&ourSuper, FSYS_INDEX_ROOT, path)) <= 0 )
	{
		if ( !strcmp(path, "/index.sys") )
			idx = FSYS_INDEX_INDEX;
		else if ( !strcmp(path,"/freemap.sys") )
			idx = FSYS_INDEX_FREE;
		else if ( !strcmp(path,"/rootdir.sys") )
			idx = FSYS_INDEX_ROOT;
		else if ( !strcmp(path, "/journal.sys") && (ourSuper.homeBlk.features&FSYS_FEATURES_JOURNAL) )
			idx = FSYS_INDEX_JOURNAL;
		else
			ret = -ENOENT;
		if ( ret < 0 )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_getattr() returned -ENOENT because '%s' could not be found\n", path);
			fflush(ourSuper.logFile);
		}
	}
	if ( !ret )
	{
		inode = ourSuper.inodeList[idx];
		if ( S_ISDIR(inode->mode) )
		{
			stbuf->st_mode = S_IFDIR | wFlags | 0555;
			stbuf->st_nlink = 2 + inode->numInodes;
		}
		else
		{
			stbuf->st_mode = S_IFREG | wFlags | 0444;
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
	}
	UNLOCK_IT("rdMutex",&ourSuper,&rdMutex);
	return ret;
}

static int mgwfs_readdir(const char *path,
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
		fprintf(ourSuper.logFile, "FUSE mgwfs_readdir(path='%s',buf=%p,offset=%ld,fi,flags=0x%X)\n", path, buf, offset,flags);
		fflush(ourSuper.logFile);
	}
	LOCK_IT("rdMutex",&ourSuper,&rdMutex);
	idx = findInode(&ourSuper,FSYS_INDEX_ROOT,path);
	if (!idx)
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_readdir() returned -ENOENT because '%s' could not be found\n", path);
		fflush(ourSuper.logFile);
		UNLOCK_IT("rdMutex",&ourSuper,&rdMutex);
		return -ENOENT;
	}
	inode = ourSuper.inodeList[idx];
	if ( !S_ISDIR(inode->mode) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_readdir() returned -ENOENT because '%s' (inode %d) is not a directory\n", path, idx);
		fflush(ourSuper.logFile);
		UNLOCK_IT("rdMutex",&ourSuper,&rdMutex);
		return -ENOENT;
	}
	filler(buf, ".", NULL, 0, FUSE_FILL_DIR_PLUS);
	filler(buf, "..", NULL, 0, FUSE_FILL_DIR_PLUS);
	idx = inode->idxChildTop;
	while ( idx )
	{
		struct stat stbuf;
		inode = ourSuper.inodeList[idx];
		memset(&stbuf, 0, sizeof(struct stat));
		if ( S_ISDIR(inode->mode) )
		{
			stbuf.st_mode = S_IFDIR | (options.read_write ? 0775 : 0555);
			stbuf.st_nlink = 2 + inode->numInodes;
		}
		else
		{
			stbuf.st_mode = S_IFREG | (options.read_write ? 0664 : 0444);
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
		fRet = filler(buf, inode->fileName, &stbuf, 0, FUSE_FILL_DIR_PLUS);
		if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_readdir(): Uploaded inode %d '%s'. Next=%d. fRet=%d\n", idx, inode->fileName, inode->idxNextInode, fRet );
			fflush(ourSuper.logFile);
		}
		if ( fRet )
			break;
		idx = inode->idxNextInode;
	}
	UNLOCK_IT("rdMutex",&ourSuper,&rdMutex);
	return 0;
}

static int mgwfs_open(const char *path, struct fuse_file_info *fi)
{
	MgwfsInode_t *inode;
	FuseFH_t *fhp;
	int idx;
	int retVal = -EINVAL;
	
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
		fprintf(ourSuper.logFile, "FUSE mgwfs_open(path='%s',fi->fh=%ld, fi->flags=0x%X)\n", path, fi->fh, fi->flags);
	do
	{
		LOCK_IT("rdMutex",&ourSuper,&rdMutex);
		idx = findInode(&ourSuper,FSYS_INDEX_ROOT,path);
		if ( options.read_write && (fi->flags & O_CREAT) )
		{
			if ( idx )
			{
				if ( (ourSuper.verbose & VERBOSE_FUSE_CMD) )
					fprintf(ourSuper.logFile, "FUSE mgwfs_open() returned -EEXIST because '%s' (inode %d) already exists.\n", path, idx);
				retVal = -EEXIST;
				break;
			}
			idx = fileCreate("mgwfs_open()", path, &ourSuper);
			if ( idx < 0 )
			{
				retVal = idx;
				break;
			}
		}
		if ( !idx )
		{
			int canOpen=0;
			if ( !(fi->flags & (O_RDWR | O_TRUNC | O_APPEND | O_WRONLY | O_CREAT )) )
			{
				canOpen = 1;
				if ( !strcmp(path, "/index.sys") )
					idx = FSYS_INDEX_INDEX;
				else if ( !strcmp(path,"/freemap.sys") )
					idx = FSYS_INDEX_FREE;
				else if ( !strcmp(path,"/rootdir.sys") )
					idx = FSYS_INDEX_ROOT;
				else if ( !strcmp(path, "/journal.sys") && (ourSuper.homeBlk.features&FSYS_FEATURES_JOURNAL) )
					idx = FSYS_INDEX_JOURNAL;
				else
					canOpen = 0;
			}
			if ( !canOpen )
			{
				fprintf(ourSuper.logFile, "FUSE mgwfs_open() returned -ENOENT because '%s' could not be found\n", path);
				retVal = -ENOENT;
				break;
			}
		}
		inode = ourSuper.inodeList[idx];
		if ( S_ISDIR(inode->mode) && (fi->flags & (O_RDWR | O_TRUNC | O_APPEND | O_WRONLY | O_CREAT )) )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_open() returned -EINVAL because '%s' (inode %d) is a directory\n", path, idx);
			retVal = -EINVAL;
			break;
		}
		fhp = getFuseFHidx(&ourSuper, 0);
		++fhp->instances;
		fhp->inode = idx;
		fhp->openFlags = fi->flags;
		fi->fh = fhp->index;
		retVal = 0;
		retVal = fileOpen("FUSE mgwfs_open()", path, &ourSuper, fhp);
		inode->rwb.buffSize = inode->fsHeader.clusters*BYTES_PER_SECTOR;
		inode->rwb.buff = (uint8_t *)malloc(inode->rwb.buffSize);
		/* Read the whole file into a local buffer */
		inode->rwb.buffErr = readWholeFile("FUSE mgwfs_open():", &ourSuper, inode->rwb.buff, inode->fsHeader.size, inode->fsHeader.pointers[0]);
		if ( inode->rwb.buffErr >= 0 )
		{
			inode->rwb.buffUsed = inode->rwb.buffErr;
			if ( (fi->flags&(O_WRONLY|O_RDWR)) )
			{
				if ( (fi->flags & O_TRUNC) )
					inode->rwb.buffUsed = 0;
				if ( (fi->flags & O_APPEND) )
					inode->rwb.buffOffset = inode->rwb.buffUsed;
			}
			if ((ourSuper.verbose&VERBOSE_FUSE_CMD))
			{
				fprintf(ourSuper.logFile, "FUSE mgwfs_open(%s,0x%X) returned success on open, inode %d and FHidx %d, rwBuff=%p, rwBuffUsed=%d, rwBuffOffset=%ld, rwBuffSize=%d\n"
						,path
						,fhp->openFlags
						,idx
						,fhp->index
						,inode->rwb.buff
						,inode->rwb.buffUsed
						,inode->rwb.buffOffset
						,inode->rwb.buffSize
						 );
			}
		}
		else
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_open('%s') readFile() returned error %d.\n", path, inode->rwb.buffErr );
		}
	} while (0);
	fflush(ourSuper.logFile);
	UNLOCK_IT("rdMutex",&ourSuper,&rdMutex);
	return retVal;
}

static int mgwfs_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	int retVal = -EIO;
	FuseFH_t *fhp;
	MgwfsInode_t *inode;
	
	LOCK_IT("rdMutex",&ourSuper,&rdMutex);
	do
	{
		size_t cpyAmt;
		
		fhp = getFuseFHidx(&ourSuper, fi->fh);
		inode = ourSuper.inodeList[fhp->inode];
		if ( !fhp )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_read('%s', %ld, 0x%lX): not opened. Returned -EIO\n"
					,path
					,size
					,offset
					);
			break;
		}
		if ( inode->rwb.buffErr < 0 )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_read('%s', %ld, 0x%lX): Found rwBuffErr=%d\n"
					,path
					,size
					,offset
					,inode->rwb.buffErr
					);
			retVal = inode->rwb.buffErr;
			break;
		}
		inode = ourSuper.inodeList[fhp->inode];
		if ( !inode )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_read('%s', %ld, 0x%lX): No inode found. Returned -EIO\n"
					,path
					,size
					,offset
					);
			break;
		}
		if ( (ourSuper.verbose & VERBOSE_FUSE_CMD) )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_read('%s', %ld, 0x%lX): fhp->rwBuffUsed=%d, rwBuffOffset=%ld, rwBuffSize=%d, rwBuffErr=%d\n"
					,path
					,size
					,offset
					,inode->rwb.buffUsed
					,inode->rwb.buffOffset
					,inode->rwb.buffSize
					,inode->rwb.buffErr
					);
			fflush(ourSuper.logFile);
		}
		cpyAmt = 0;
		if ( inode->rwb.buffUsed > 0 )
		{
			off_t adjOffset = offset;
			cpyAmt = size;
			if ( adjOffset > inode->rwb.buffUsed )
				adjOffset = inode->rwb.buffUsed;
			if ( cpyAmt + adjOffset > inode->rwb.buffUsed )
				cpyAmt = inode->rwb.buffUsed-adjOffset;
			if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
			{
				fprintf(ourSuper.logFile, "FUSE mgwfs_read('%s', %ld, 0x%lX) cpyAmt=%ld, adjOffset=%ld, rwBuffUsed=%d\n",
						path,
						size,
						offset,
						cpyAmt,
						adjOffset,
						inode->rwb.buffUsed );
			}
			if ( cpyAmt > 0 )
			{
				memcpy(buf, inode->rwb.buff + adjOffset, cpyAmt);
				inode->rwb.buffOffset += cpyAmt;
			}
		}
		retVal = cpyAmt;
	} while (0);
	if ( retVal < 0 )
		fflush(ourSuper.logFile);
	UNLOCK_IT("rdMutex",&ourSuper,&rdMutex);
	return retVal;
}

static int mgwfs_release(const char *path, struct fuse_file_info *fi)
{
	int sts=0;
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_release(path='%s',fi->fh=%ld\n", path, fi->fh);
		fflush(ourSuper.logFile);
	}
	if ( fi->fh )
	{
		FuseFH_t *fhp;
		
		LOCK_IT("rdMutex",&ourSuper,&rdMutex);
		fhp = getFuseFHidx(&ourSuper,fi->fh);
		if ( --fhp->instances <= 0 )
		{
			sts = fileClose("mgwfs_release():",&ourSuper,fhp);
			freeFuseFHidx(&ourSuper, fi->fh);
			fi->fh = 0;
		}
		UNLOCK_IT("rdMutex",&ourSuper,&rdMutex);
	}
	return sts;
}

/* Should be set to BYTES_PER_SECTOR, but it doesn't like that */
#define BLOCK_SIZE (4096)

static int mgwfs_statfs(const char *path, struct statvfs *stp)
{
	FsysRetPtr *rp;
	FreeMap_t *freeMap = &ourSuper.freeMap;
	
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_statfs('%s',%p\n", path, stp);
		fflush(ourSuper.logFile);
	}
//	memset(stp,0,sizeof(statvfs));
	stp->f_type = ANON_INODE_FS_MAGIC;
	stp->f_bsize = BLOCK_SIZE; //BYTES_PER_SECTOR;
	stp->f_blocks = (ourSuper.homeBlk.max_lba*BYTES_PER_SECTOR)/BLOCK_SIZE;
	rp = (FsysRetPtr *)freeMap->rwBuff.buff;
	while ( rp && rp < (FsysRetPtr *)freeMap->rwBuff.buff + freeMap->freeMapEntriesAvail && rp->nblocks )
	{
		stp->f_bfree += rp->nblocks;
		++rp;
	}
	stp->f_bavail = stp->f_blocks
		- (1+
		   (( ourSuper.inodeList[FSYS_INDEX_INDEX]->fsHeader.size
			 +ourSuper.inodeList[FSYS_INDEX_FREE]->fsHeader.size
			 +ourSuper.inodeList[FSYS_INDEX_ROOT]->fsHeader.size
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

static int mgwfs_access(const char *path, int flags)
{
	int idx;
	
	LOCK_IT("rdMutex",&ourSuper,&rdMutex);
	idx = findInode(&ourSuper,FSYS_INDEX_ROOT,path);
	UNLOCK_IT("rdMutex",&ourSuper,&rdMutex);
	return idx ? 0 : -ENOENT;
}

/*
 * Detach inode 'idx' from its parent directory, free its file-header sectors
 * and data sectors back to the freemap, drop it from the inode list and mark
 * the affected metadata (parent dir, index.sys, freemap.sys) dirty. The caller
 * must hold rdMutex and must already have verified that idx is a valid, removable
 * (non-directory) inode. Returns 0 on success or a negative errno.
 */
static int detachInode(MgwfsSuper_t *super, int idx, const char *path)
{
	MgwfsInode_t *parent, *prev, *curr, *next;
	FsysRetPtr *rp, tmp;
	IndexSys_t *indexPtr;
	int ii, jj, verbLen=0;
	char verbBuff[200];

	curr = super->inodeList[idx];
	// Need to remove the filename from the directory to which this file is listed
	/* Assume no pointers */
	parent = NULL;
	prev = NULL;
	next = NULL;
	addToDirty("detachInode():", super,curr->idxParentInode);
	/* Get pointer to previous inode if there is one */
	if ( curr->idxPrevInode )
		prev = super->inodeList[curr->idxPrevInode];
	/* Get pointer to next inode if there is one */
	if ( curr->idxNextInode )
		next = super->inodeList[curr->idxNextInode];
	/* If there's no previous, then point to the parent */
	if ( !prev )
		parent = super->inodeList[curr->idxParentInode];
	/* If there's a next, then its previous gets our previous */
	if ( next )
		next->idxPrevInode = curr->idxPrevInode;
	/* If there's a previous, then its next gets our next  */
	if ( prev )
		prev->idxNextInode = curr->idxNextInode;
	else if ( parent )
	{
		/* If there's no previous, then get parent's child becomes our next */
		parent->idxChildTop = curr->idxNextInode;
	}
	else
	{
		// FATAL! Check for fatal errror here. There has to always be a parent
		fprintf(super->logFile, "detachInode('%s') returned -EIO because (inode %d) has no parent entry\n", path, idx);
		fprintf(super->errFile, "detachInode('%s') returned -EIO because (inode %d) has no parent entry\n", path, idx);
		return -EIO;
	}
	tmp.nblocks = 1;
	// Need to free the sectors assigned to the file headers assigned to this file
	indexPtr = super->indexSys+curr->inode_no;
	if ( (super->verbose&VERBOSE_FUSE_CMD) )
	{
		verbLen = snprintf(verbBuff,sizeof(verbBuff), "detachInode('%s'): free sectors ",
				path);
	}
	for (ii=0; ii < FSYS_MAX_ALTS; ++ii)
	{
		if ( ((tmp.start = indexPtr->lba[ii])&FSYS_LBA_MASK) )
		{
			if ( verbLen )
			{
				verbLen += snprintf(verbBuff+verbLen,sizeof(verbBuff)-verbLen,
								 " 0x%08X/%d",
									tmp.start,
									tmp.nblocks);
			}
			mgwfsFreeSectors(super,&tmp,FALSE);
			// Need to mark the entries in index.sys as available
			indexPtr->lba[ii] = FSYS_EMPTYLBA_BIT;
		}
	}
	// Need to free the sectors assigned to this file
	for (ii=0; ii < FSYS_MAX_ALTS; ++ii)
	{
		rp = curr->fsHeader.pointers[ii];
		for ( jj = 0; rp->nblocks && jj < FSYS_MAX_FHPTRS; ++jj, ++rp )
		{
			if ( verbLen )
			{
				verbLen += snprintf(verbBuff+verbLen,sizeof(verbBuff)-verbLen,
								 " 0x%08X/%d",
									rp->start,
									rp->nblocks);
			}
			mgwfsFreeSectors(super, rp, TRUE);
		}
	}
	for (ii=0; ii < MAX_NUM_BOOT_FILES; ++ii)
	{
		if ( super->bootIndicies[ii] == curr->inode_no )
		{
			IndexSys_t *ip;
			super->bootIndicies[ii] = 0;
			switch (ii)
			{
			case 0:
				ip = (IndexSys_t *)super->homeBlk.boot;
				break;
			case 1:
				ip = (IndexSys_t *)super->homeBlk.boot1;
				break;
			case 2:
				ip = (IndexSys_t *)super->homeBlk.boot2;
				break;
			case 3:
				ip = (IndexSys_t *)super->homeBlk.boot3;
				break;
			}
			memset(ip,0,sizeof(IndexSys_t));
			super->specialDirtys |= SPECIAL_DIRTY_HOME;
		}
	}
	if ( verbLen )
		fprintf(super->logFile, "%s\n", verbBuff);
	/* Keep the inode's own header-LBA copy consistent with the now-empty
	 * index.sys slot (index.sys is rebuilt from inode->fhSectors). */
	super->inodeList[idx] = NULL;
	free(curr);
	addToDirty("detachInode():", super,FSYS_INDEX_INDEX);
	addToDirty("detachInode():", super,FSYS_INDEX_FREE);
	return 0;
}

static int mgwfs_unlink(const char *path)
{
	int retVal, idx;
	MgwfsSuper_t *super = &ourSuper;

	if ( (super->verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(super->logFile, "FUSE mgwfs_unlink('%s')\n", path );
		fflush(super->logFile);
	}
	LOCK_IT("rdMutex",&ourSuper,&rdMutex);
	idx = findInode(&ourSuper,FSYS_INDEX_ROOT,path);
	if ( !idx )
	{
		if ( (super->verbose&VERBOSE_FUSE_CMD) )
			fprintf(super->logFile, "FUSE mgwfs_unlink('%s') returned ENOENT\n", path );
		retVal = -ENOENT;
	}
	else if ( S_ISDIR(super->inodeList[idx]->mode) )
	{
		if ( (super->verbose&VERBOSE_FUSE_CMD) )
			fprintf(super->logFile, "FUSE mgwfs_unlink() returned -EINVAL because '%s' (inode %d) is a directory\n", path, idx);
		retVal = -EINVAL;
	}
	else
	{
		retVal = detachInode(super, idx, path);
	}
	fflush(super->logFile);
	UNLOCK_IT("rdMutex",&ourSuper,&rdMutex);
	return retVal;
}

static off_t addToBuff(RwBuff_t *rwb, const char *path, off_t off)
{
	if ( off >= rwb->buffSize )
	{
		uint8_t *newPtr;
		int sectors = (off+BYTES_PER_SECTOR-1)/BYTES_PER_SECTOR;
		int bytes = sectors*BYTES_PER_SECTOR;
		newPtr = (uint8_t *)realloc(rwb->buff, bytes);
		if ( !newPtr )
		{
			fprintf(ourSuper.logFile, "FUSE addToBuff(%s,0x%lX). Out of memory to allocate %d bytes.\n", path, off, bytes );
			return -ENOMEM;
		}
		rwb->buff = newPtr;
		rwb->buffSize = bytes;
		memset(rwb->buff+rwb->buffUsed,0,rwb->buffSize-rwb->buffUsed);
	}
	if ( off > rwb->buffUsed )
		rwb->buffUsed = off;
	rwb->buffOffset = off;
	return off;
}

static int mgwfs_write (const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	MgwfsInode_t *inode;
	FuseFH_t *fhp=NULL;
	int cpyAmt= -EIO;
	
	do
	{
		LOCK_IT("rdMutex",&ourSuper,&rdMutex);
		if ( !fi->fh )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_write('%s',%p,%ld,0x%lX,%ld) returned -EPERM because has not been open()'d\n", path, buf, size, offset, fi->fh);
			cpyAmt = -EPERM;
			break;
		}
		fhp = getFuseFHidx(&ourSuper,fi->fh);
		inode = ourSuper.inodeList[fhp->inode];
		if ( inode->fsHeader.type == FSYS_TYPE_DIR )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_write('%s') returned -EISDIR because writes to a directory are not allowed\n",
					path);
			cpyAmt = -EISDIR;
			break;
		}
		if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_write('%s',%p,%ld,0x%lX,%ld): Before: rwBuff=%p, rwBuffUsed=%d, rwBuffOffset=%ld, rwBuffSize=%d\n"
					,path
					,buf
					,size
					,offset
					,fi->fh
					,inode->rwb.buff
					,inode->rwb.buffUsed
					,inode->rwb.buffOffset
					,inode->rwb.buffSize
					);
			fflush(ourSuper.logFile);
		}
		if ( size + offset > inode->rwb.buffSize )
		{
			off_t currOffset = inode->rwb.buffOffset;
			cpyAmt = addToBuff(&inode->rwb,path,offset+size);
			if ( cpyAmt < 0 )
				break;
			inode->rwb.buffOffset = currOffset;
		}
		cpyAmt = size;
		memcpy(inode->rwb.buff + inode->rwb.buffOffset, buf, cpyAmt);
		if ( (ourSuper.verbose&(VERBOSE_FUSE_CMD|VERBOSE_WRITES)) )
		{
			int idx, bcnt = cpyAmt;
			if ( bcnt > 5 )
				bcnt = 5;
			fprintf(ourSuper.logFile, "FUSE mgwfs_write('%s'): added (%d bytes)"
					,path
					,cpyAmt
					);
			for (idx=0; idx < bcnt; ++idx)
				fprintf(ourSuper.logFile, " %02X", buf[idx]);
			fprintf(ourSuper.logFile,"%s at offset %ld\n"
					,bcnt != cpyAmt ? "..." : ""
					,inode->rwb.buffOffset
					);
			fflush(ourSuper.logFile);
		}
		inode->rwb.buffOffset += cpyAmt;
		if ( inode->rwb.buffOffset > inode->rwb.buffUsed )
			inode->rwb.buffUsed = inode->rwb.buffOffset;
		if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_write('%s',%p,%ld,0x%lX,%ld): After:  rwBuff=%p, rwBuffUsed=%d, rwBuffOffset=%ld, rwBuffSize=%d\n"
					,path
					,buf
					,size
					,offset
					,fi->fh
					,inode->rwb.buff
					,inode->rwb.buffUsed
					,inode->rwb.buffOffset
					,inode->rwb.buffSize
					);
			fflush(ourSuper.logFile);
		}
	} while (0);
	UNLOCK_IT("rdMutex",&ourSuper,&rdMutex);
	fflush(ourSuper.logFile);
	return cpyAmt;
}

static int mgwfs_flush(const char *path, struct fuse_file_info *fi)
{
	FuseFH_t *fhp;
	int sts;
	
	if ( !fi->fh )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_flush('%s',%ld) returned -EPERM because has not been open()'d\n", path, fi->fh);
		return -EPERM;
	}
	LOCK_IT("rdMutex",&ourSuper,&rdMutex);
	fhp = getFuseFHidx(&ourSuper,fi->fh);
	if ( fhp )
	{
		MgwfsInode_t *inode;
		inode = ourSuper.inodeList[fhp->inode];
		sts = fileFlush("mgwfs_flush()", &ourSuper, fhp);
		if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_flush('%s',%ld): rwBuff=%p, rwBuffUsed=%d, rwBuffOffset=%ld, rwBuffSize=%d\n"
					,path
					,fi->fh
					,inode->rwb.buff
					,inode->rwb.buffUsed
					,inode->rwb.buffOffset
					,inode->rwb.buffSize
					);
			fflush(ourSuper.logFile);
		}
	}
	else
		sts = -EIO;
	UNLOCK_IT("rdMutex",&ourSuper,&rdMutex);
	return sts;
}

static int mgwfs_fsync(const char *path, int arg, struct fuse_file_info *fi)
{
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_fsync(path='%s', fi->fh=%ld\n", path, fi->fh );
		fflush(ourSuper.logFile);
	}
	if ( options.read_write )
		updateAllMetaData("FUSE mgwfs_fsync()", &ourSuper);
	return 0;
}

static void mgwfs_destroy(void *private_data)
{
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_destroy(), pd=%p, &ourSuper=%p\n", private_data, &ourSuper );
		fflush(ourSuper.logFile);
	}
	/* Flush any dirty metadata (e.g. headers whose mtime was changed via
	 * utimens but never went through a file close) before we go away. */
	if ( options.read_write )
		updateAllMetaData("FUSE mgwfs_destroy()", &ourSuper);
}

/* Remove an inode from its parent directory's child list only (no sector
 * freeing, the inode itself survives). Caller holds rdMutex and is responsible
 * for marking the old parent dirty. */
static void unlinkFromParent(MgwfsSuper_t *super, MgwfsInode_t *curr)
{
	MgwfsInode_t *prev=NULL, *next=NULL, *parent=NULL;

	if ( curr->idxPrevInode )
		prev = super->inodeList[curr->idxPrevInode];
	if ( curr->idxNextInode )
		next = super->inodeList[curr->idxNextInode];
	if ( !prev )
		parent = super->inodeList[curr->idxParentInode];
	if ( next )
		next->idxPrevInode = curr->idxPrevInode;
	if ( prev )
		prev->idxNextInode = curr->idxNextInode;
	else if ( parent )
		parent->idxChildTop = curr->idxNextInode;
}

/* Insert an inode at the top of a directory's child list (matching the order
 * insertIntoDir() uses on create). Caller holds rdMutex and marks parent dirty. */
static void insertIntoParent(MgwfsSuper_t *super, MgwfsInode_t *parent, MgwfsInode_t *child)
{
	child->idxParentInode = parent->inode_no;
	child->idxPrevInode = 0;
	child->idxNextInode = parent->idxChildTop;
	if ( parent->idxChildTop )
	{
		MgwfsInode_t *first = super->inodeList[parent->idxChildTop];
		if ( first )
			first->idxPrevInode = child->inode_no;
	}
	parent->idxChildTop = child->inode_no;
}

// Flags can be one of:
//RENAME_NOREPLACE is set and the target exists, return an error (e.g., EEXIST).
//RENAME_EXCHANGE is set, both files must exist, and they are swapped
static int mgwfs_rename (const char *oldName, const char *newName, unsigned int flags)
{
	MgwfsSuper_t *super = &ourSuper;
	int retVal = 0, oldIdx, newIdx, newParentIdx;
	MgwfsInode_t *oldInode, *newParent;
	char *parentPath = NULL;
	const char *baseName, *slash;

	if ( (super->verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(super->logFile, "FUSE mgwfs_rename('%s','%s',0x%X)\n", oldName, newName, flags );
		fflush(super->logFile);
	}
	if ( !options.read_write )
		return -EROFS;
#ifdef RENAME_EXCHANGE
	if ( (flags & RENAME_EXCHANGE) )		/* atomic swap not supported */
		return -ENOSYS;
#endif
	LOCK_IT("rdMutex",&ourSuper,&rdMutex);
	do
	{
		oldIdx = findInode(super, FSYS_INDEX_ROOT, oldName);
		if ( oldIdx <= 0 )
		{
			retVal = -ENOENT;
			break;
		}
		oldInode = super->inodeList[oldIdx];
		/* If the target already exists, either reject (NOREPLACE) or remove it
		 * so the new name is free. We don't support replacing a directory. */
		newIdx = findInode(super, FSYS_INDEX_ROOT, newName);
		if ( newIdx > 0 )
		{
#ifdef RENAME_NOREPLACE
			if ( (flags & RENAME_NOREPLACE) )
			{
				retVal = -EEXIST;
				break;
			}
#endif
			if ( S_ISDIR(super->inodeList[newIdx]->mode) )
			{
				retVal = -EISDIR;
				break;
			}
			if ( newIdx == oldIdx )		/* renaming onto itself: nothing to do */
				break;
			retVal = detachInode(super, newIdx, newName);
			if ( retVal < 0 )
				break;
		}
		/* Split newName into its parent directory path and final component. */
		parentPath = strdup(newName);
		if ( !parentPath )
		{
			retVal = -ENOMEM;
			break;
		}
		slash = strrchr(newName, '/');
		baseName = slash ? slash+1 : newName;
		if ( !slash || slash == newName )
			parentPath[1] = 0;			/* parent is the root "/" */
		else
			parentPath[slash - newName] = 0;
		newParentIdx = findInode(super, FSYS_INDEX_ROOT, parentPath);
		if ( newParentIdx <= 0 )
		{
			retVal = -ENOENT;
			break;
		}
		newParent = super->inodeList[newParentIdx];
		if ( !S_ISDIR(newParent->mode) )
		{
			retVal = -ENOTDIR;
			break;
		}
		/* Detach from the old directory, rename, and attach to the new one. The
		 * name lives only in the directory entries, so marking both directories
		 * dirty is what makes the change persist. */
		addToDirty("mgwfs_rename(): old parent", super, oldInode->idxParentInode);
		unlinkFromParent(super, oldInode);
		strncpy(oldInode->fileName, baseName, sizeof(oldInode->fileName)-1);
		oldInode->fileName[sizeof(oldInode->fileName)-1] = 0;
		oldInode->fnLen = strlen(oldInode->fileName);
		insertIntoParent(super, newParent, oldInode);
		addToDirty("mgwfs_rename(): new parent", super, newParent->inode_no);
		/* A moved directory's synthesized ".." comes from idxParentInode, so it
		 * must be repacked when reparented. */
		if ( S_ISDIR(oldInode->mode) )
			addToDirty("mgwfs_rename(): moved dir", super, oldInode->inode_no);
		retVal = 0;
	} while ( 0 );
	free(parentPath);
	fflush(super->logFile);
	UNLOCK_IT("rdMutex",&ourSuper,&rdMutex);
	return retVal;
}

static int mgwfs_mkdir(const char *path, mode_t mode)
{
	MgwfsSuper_t *super = &ourSuper;
	int retVal = 0, idx, parentIdx;
	MgwfsInode_t *inode, *parent;
	char *parentPath = NULL;
	const char *baseName, *slash;

	if ( (super->verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(super->logFile, "FUSE mgwfs_mkdir('%s',0x%X)\n", path, mode);
		fflush(super->logFile);
	}
	if ( !options.read_write )
		return -EROFS;
	LOCK_IT("rdMutex",&ourSuper,&rdMutex);
	do
	{
		/* Reject if anything already lives at this path. */
		idx = findInode(super, FSYS_INDEX_ROOT, path);
		if ( idx > 0 )
		{
			retVal = -EEXIST;
			break;
		}
		/* Split path into its parent directory and the final component. */
		parentPath = strdup(path);
		if ( !parentPath )
		{
			retVal = -ENOMEM;
			break;
		}
		slash = strrchr(path, '/');
		baseName = slash ? slash+1 : path;
		if ( !slash || slash == path )
			parentPath[1] = 0;			/* parent is the root "/" */
		else
			parentPath[slash - path] = 0;
		if ( !*baseName )				/* trailing slash, no name to create */
		{
			retVal = -EINVAL;
			break;
		}
		if ( strlen(baseName) > MGWFS_FILENAME_MAXLEN )
		{
			retVal = -ENAMETOOLONG;
			break;
		}
		parentIdx = findInode(super, FSYS_INDEX_ROOT, parentPath);
		if ( parentIdx <= 0 )
		{
			retVal = -ENOENT;
			break;
		}
		parent = super->inodeList[parentIdx];
		if ( !S_ISDIR(parent->mode) )
		{
			retVal = -ENOTDIR;
			break;
		}
		/* Allocate a fresh inode and turn it into an (empty) directory. */
		inode = findUnusedInode(super);
		if ( !inode )
		{
			retVal = -ENOSPC;
			break;
		}
		strncpy(inode->fileName, baseName, sizeof(inode->fileName)-1);
		inode->fileName[sizeof(inode->fileName)-1] = 0;
		inode->fnLen = strlen(inode->fileName);
		inode->fsHeader.type = FSYS_TYPE_DIR;
		inode->mode = S_IFDIR | 0775;
		/* Link it into its parent, then schedule both the new directory (so its
		 * synthesized "." / ".." entries get packed and written, which also
		 * allocates its header and data sectors) and the parent (whose contents
		 * now list the new name) for write-back. */
		insertIntoParent(super, parent, inode);
		addToDirty("mgwfs_mkdir(): parent", super, parentIdx);
		addToDirty("mgwfs_mkdir(): new dir", super, inode->inode_no);
		retVal = 0;
	} while ( 0 );
	free(parentPath);
	if ( (super->verbose&VERBOSE_FUSE_CMD) )
		fprintf(super->logFile, "FUSE mgwfs_mkdir('%s') returned %d\n", path, retVal);
	fflush(super->logFile);
	UNLOCK_IT("rdMutex",&ourSuper,&rdMutex);
	return retVal;
}

static int mgwfs_rmdir(const char *path)
{
	MgwfsSuper_t *super = &ourSuper;
	int retVal = 0, idx;
	MgwfsInode_t *inode;

	if ( (super->verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(super->logFile, "FUSE mgwfs_rmdir('%s')\n", path);
		fflush(super->logFile);
	}
	if ( !options.read_write )
		return -EROFS;
	LOCK_IT("rdMutex",&ourSuper,&rdMutex);
	do
	{
		idx = findInode(super, FSYS_INDEX_ROOT, path);
		if ( idx <= 0 )
		{
			retVal = -ENOENT;
			break;
		}
		if ( idx == FSYS_INDEX_ROOT )		/* the root directory can't be removed */
		{
			retVal = -EBUSY;
			break;
		}
		inode = super->inodeList[idx];
		if ( !S_ISDIR(inode->mode) )
		{
			retVal = -ENOTDIR;
			break;
		}
		if ( inode->idxChildTop )			/* directory still has entries */
		{
			retVal = -ENOTEMPTY;
			break;
		}
		/* detachInode unlinks it from the parent's child list, frees its
		 * file-header and data sectors back to the freemap and drops the inode,
		 * marking the parent, index.sys and freemap.sys dirty. */
		retVal = detachInode(super, idx, path);
	} while ( 0 );
	if ( (super->verbose&VERBOSE_FUSE_CMD) )
		fprintf(super->logFile, "FUSE mgwfs_rmdir('%s') returned %d\n", path, retVal);
	fflush(super->logFile);
	UNLOCK_IT("rdMutex",&ourSuper,&rdMutex);
	return retVal;
}

static int mgwfs_create(const char *path, mode_t fMode, struct fuse_file_info *fi)
{
	int idx, sts=0;
	idx = fileCreate("mgwfs_create()", path, &ourSuper);
	if ( idx < 0 )
		sts = idx;
	else
	{
		FuseFH_t *fhp;
		fhp = getFuseFHidx(&ourSuper, 0);
		++fhp->instances;
		fhp->inode = idx;
		fhp->openFlags = fi->flags;
		fi->fh = fhp->index;
	}
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_create('%s',0x%X,%ld), idx=%d, sts=%d\n", path, fMode, fi->fh, idx, sts);
		fflush(ourSuper.logFile);
	}
	return sts;
}

static off_t moveOffset(const char *path, FuseFH_t *fhp, off_t off)
{
	MgwfsInode_t *inode;
	
	inode = ourSuper.inodeList[fhp->inode];
	if ( off < 0 )
		off = 0;
	if ( off <= inode->rwb.buffUsed )
	{
		inode->rwb.buffOffset = off;
		return off;
	}
	if ( !(fhp->openFlags & (O_RDWR | O_WRONLY)) )
	{
		/* read only, cannot go past EOF */
		if ( off >= inode->rwb.buffUsed )
			off = inode->rwb.buffUsed;
		inode->rwb.buffOffset = off;
		return off;
	}
	/* r/w or wo, maybe add to end of file */
	return addToBuff(&inode->rwb,path,off);
}

static off_t lseek_locked(const char *path, off_t off, int whence, struct fuse_file_info *fi)
{
	off_t newOff, sts = -EIO;

	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		static const char *Seeks[] = { "SEEK_SET", "SEEK_CUR", "SEEK_END" };
		const char *sp;
		char tmp[32];
		int idx = whence;
		
		if ( idx >= n_elts(Seeks) )
		{
			snprintf(tmp,sizeof(tmp),"SEEK_%d",idx);
			sp = tmp;
		}
		else
			sp = Seeks[whence];
		fprintf(ourSuper.logFile, "FUSE lseek_locked(%s,%ld,%s,%ld)\n", path, off, sp, fi->fh);
		fflush(ourSuper.logFile);
	}
	if ( fi->fh )
	{
		FuseFH_t *fhp;
		MgwfsInode_t *inode;
		
		fhp = getFuseFHidx(&ourSuper,fi->fh);
		inode = ourSuper.inodeList[fhp->inode];
		if ( fhp )
		{
			switch (whence)
			{
			case SEEK_SET:
				sts = moveOffset(path,fhp,off);
				break;

			case SEEK_CUR:
				newOff = inode->rwb.buffOffset+off;
				sts = moveOffset(path,fhp,newOff);
				break;

			case SEEK_END:
				newOff = inode->rwb.buffUsed+off;
				sts = moveOffset(path,fhp,newOff);
				break;
				
			default:
				break;
			}
		}
	}
	return sts;
}

static off_t mgwfs_lseek(const char *path, off_t off, int whence, struct fuse_file_info *fi)
{
	off_t sts;
	LOCK_IT("rdMutex",&ourSuper,&rdMutex);
	sts = lseek_locked(path,off,whence,fi);
	UNLOCK_IT("rdMutex",&ourSuper,&rdMutex);
	return sts;
}

static int mgwfs_truncate(const char *path, off_t offset, struct fuse_file_info *fi)
{
	int sts = options.read_write ? -EPERM : -EIO;

	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_truncate(%s,0x%lX,%ld)\n", path, offset, fi->fh );
		fflush(ourSuper.logFile);
	}
	if ( options.read_write && fi->fh )
	{
		FuseFH_t *fhp;
		MgwfsInode_t *inode;
		
		LOCK_IT("rdMutex",&ourSuper,&rdMutex);
		fhp = getFuseFHidx(&ourSuper, fi->fh);
		inode = ourSuper.inodeList[fhp->inode];
		if ( (fhp->openFlags & (O_RDWR|O_WRONLY)) )
		{
			sts = moveOffset(path,fhp,offset);
			if ( sts >= 0 )
			{
				inode->rwb.buffUsed = offset;
				inode->rwb.buffOffset = offset;
			}
		}
		UNLOCK_IT("rdMutex",&ourSuper,&rdMutex);
	}
	return sts;
}

/*
  Comments from Claude which added this function:
  
  What I did

  1. Added the real mgwfst_utimens definition (fuse.c, just above the mgwfs_oper table). It follows the same shape as mgwfs_getattr: verbose logging, read_write guard (returns -EROFS on a read-only mount), findInode lookup under
  rdMutex, and the -ENOENT log message on a miss. It correctly handles UTIME_NOW, UTIME_OMIT, and a NULL tv, stores the resolved mtime into inode->fsHeader.mtime, and calls addToDirty(...) to schedule the header for write-back — same
  mechanism every other mutator here uses.
  2. Moved .utimens = mgwfst_utimens out of the #if 0 block so it's actually registered in the operations table.

  Two caveats you should know about

  These aren't bugs in the wiring — they're consequences of the current filesystem state, and both are honestly reflected in the code comments:

  - No atime storage. agcfsys.h only has uint32_t ctime/mtime, so the access time (tv[0]) is resolved but then dropped. This matches getattr, which never fills st_atime. Nanoseconds are also truncated since the format holds whole
  seconds only.
  - updateAllMetaData() will clobber an explicit mtime. At mgwfs.c:1018 the flush path unconditionally does inode->fsHeader.mtime = time(NULL) for normal inodes. So once disk write-back actually works (writeFileHeader is still a stub
  returning EIO), a touch -d '2001-01-01' file would persist as now rather than the requested time. Within a live mount it's fine — getattr reads the in-memory header directly, so stat reflects the set time immediately. But the
  persisted value won't survive a flush of an explicit (non-now) timestamp.

  If you want, I can make updateAllMetaData preserve an explicitly-set mtime (e.g. a small "mtime already set this round" flag, or a separate dirty reason that skips the time(NULL) stamp) so arbitrary timestamps round-trip correctly
  once writeFileHeader is implemented.
*/

/*
 * int (*utimens)(const char *, const struct timespec tv[2], struct fuse_file_info *fi);
 *
 * Modern replacement for the old clib-style utime() FUSE op. tv[0] is the
 * access time, tv[1] is the modification time. Either tv_nsec field may be
 * the special value UTIME_NOW (set to current time) or UTIME_OMIT (leave
 * unchanged); when tv itself is NULL both stamps are set to "now" (the
 * classic utime(path, NULL) / touch case).
 *
 * The on-disk format (agcfsys.h) keeps only a 32-bit mtime/ctime and has no
 * atime field, so the access time is resolved but then discarded. getattr()
 * likewise leaves st_atime zero, so the two stay consistent.
 */
static int mgwfst_utimens(const char *path, const struct timespec tv[2],
						  struct fuse_file_info *fi)
{
	int idx, ret=0;
	MgwfsInode_t *inode;

	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfst_utimens(path='%s')\n", path);
		fflush(ourSuper.logFile);
	}
	if ( !options.read_write )
		return -EROFS;
	LOCK_IT("rdMutex",&ourSuper,&rdMutex);
	if ( (idx = findInode(&ourSuper, FSYS_INDEX_ROOT, path)) <= 0 )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfst_utimens() returned -ENOENT because '%s' could not be found\n", path);
		fflush(ourSuper.logFile);
		ret = -ENOENT;
	}
	else
	{
		time_t now = time(NULL);	/* used for UTIME_NOW and a NULL tv */
		time_t atime, mtime;

		inode = ourSuper.inodeList[idx];

		/* Resolve access time (tv[0]). No atime field on media, so it is
		 * computed for completeness but ultimately dropped. */
		if ( tv == NULL || tv[0].tv_nsec == UTIME_NOW )
			atime = now;
		else if ( tv[0].tv_nsec == UTIME_OMIT )
			atime = (time_t)-1;		/* sentinel: leave unchanged */
		else
			atime = tv[0].tv_sec;
		(void)atime;

		/* Resolve modification time (tv[1]) and store it (nanoseconds are
		 * truncated; the format only holds whole seconds). */
		if ( tv == NULL || tv[1].tv_nsec == UTIME_NOW )
			mtime = now;
		else if ( tv[1].tv_nsec == UTIME_OMIT )
			mtime = (time_t)-1;
		else
			mtime = tv[1].tv_sec;

		if ( mtime != (time_t)-1 )
		{
			inode->fsHeader.mtime = (uint32_t)mtime;
			/* Tell the flush path this is the time we want kept, so it
			 * won't be overwritten with "now". */
			inode->flags |= MGWFS_INODE_MTIME_SET;
			/* Schedule the file header for write-back to media. */
			addToDirty("mgwfst_utimens()", &ourSuper, idx);
		}
	}
	UNLOCK_IT("rdMutex",&ourSuper,&rdMutex);
	return ret;
}

/*
 * int (*chmod)(const char *, mode_t, struct fuse_file_info *fi);
 *
 * The on-disk header in this build (FSYS_FEATURES == FSYS_FEATURES_CMTIME)
 * carries no permission field, and getattr() synthesizes a fixed mode from the
 * inode type, so there is nowhere to record the requested bits and nothing that
 * would reflect them. We therefore accept the call and discard the mode, the
 * same way mgwfst_utimens() resolves but drops the access time.
 *
 * This is not cosmetic: rsync's do_mkstemp() does mkstemp() followed by
 * fchmod(), and with --perms (implied by -a) it treats an fchmod failure as
 * fatal -- it closes and unlinks the temp file and reports "mkstemp ... failed".
 * With no .chmod handler the kernel returns -ENOSYS for the fchmod, so
 * "rsync -a" could never land a file. Returning success here unblocks it (and
 * any other "create temp, set perms, rename" writer such as install/cp -p).
 */
static int mgwfs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	int idx, ret=0;

	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_chmod(path='%s',mode=0%o)\n", path, mode);
		fflush(ourSuper.logFile);
	}
	if ( !options.read_write )
		return -EROFS;
	LOCK_IT("rdMutex",&ourSuper,&rdMutex);
	if ( (idx = findInode(&ourSuper, FSYS_INDEX_ROOT, path)) <= 0 )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_chmod() returned -ENOENT because '%s' could not be found\n", path);
		fflush(ourSuper.logFile);
		ret = -ENOENT;
	}
	/* No perms field on media; accept and ignore the requested mode. */
	UNLOCK_IT("rdMutex",&ourSuper,&rdMutex);
	return ret;
}

/*
 * int (*chown)(const char *, uid_t, gid_t, struct fuse_file_info *fi);
 *
 * Likewise there is no owner/group on media, and getattr() always reports the
 * mounting user's uid/gid. We accept the call and discard the ids. uid/gid of
 * (uid_t)-1 means "leave unchanged" per chown(2); since we store nothing, that
 * distinction is moot, but returning success keeps "rsync -a" (which carries
 * -o/-g) and similar tools from emitting failures for every file.
 */
static int mgwfs_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi)
{
	int idx, ret=0;

	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_chown(path='%s',uid=%d,gid=%d)\n", path, (int)uid, (int)gid);
		fflush(ourSuper.logFile);
	}
	if ( !options.read_write )
		return -EROFS;
	LOCK_IT("rdMutex",&ourSuper,&rdMutex);
	if ( (idx = findInode(&ourSuper, FSYS_INDEX_ROOT, path)) <= 0 )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_chown() returned -ENOENT because '%s' could not be found\n", path);
		fflush(ourSuper.logFile);
		ret = -ENOENT;
	}
	/* No owner/group field on media; accept and ignore the requested ids. */
	UNLOCK_IT("rdMutex",&ourSuper,&rdMutex);
	return ret;
}

#define FILO_MAX_ENTRIES (16)

static void buildBootFNPath(char *dst, int maxLen, MgwfsInode_t *inode)
{
	MgwfsInode_t *filo[FILO_MAX_ENTRIES];
	int len=0, depth=0;
	
	while ( depth < FILO_MAX_ENTRIES )
	{
		filo[depth] = inode;
		++depth;
		if ( inode->idxParentInode == FSYS_INDEX_ROOT )
			break;
		inode = ourSuper.inodeList[inode->idxParentInode];
	}
	if ( depth >= FILO_MAX_ENTRIES )
		len += snprintf(dst+len,maxLen-len,"... /");
	while ( depth > 0 )
	{
		--depth;
		inode = filo[depth];
		len += snprintf(dst+len, maxLen-len, "%s%s", inode->fileName, S_ISDIR(inode->mode) ? "/":"");
	}
}

static int mgwfs_ioctl(const char *path, int cmd, void *arg,
					   struct fuse_file_info *fi, unsigned int flags, void *data)
{
	int idx, ii, sts = 0;
	MgwfsInode_t *inode;
	static const char Title[] = "mgwfs_ioctl";
	
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE %s('%s', cmd=0x%08X, flags=0x%X)\n", Title, path, cmd, flags);
		fflush(ourSuper.logFile);
	}
	/* We don't support the 32-bit-on-64-bit compat path. */
	if ( (flags & FUSE_IOCTL_COMPAT) )
		return -ENXIO;
	LOCK_IT("rdMutex", &ourSuper, &rdMutex);
	switch (cmd)
	{
	case MGWFS_IOC_GETSTATS:
		{
			MgwfsIoctlStats_t *st = (MgwfsIoctlStats_t *)data;
			st->hbMajor = ourSuper.homeBlk.hb_major;
			st->hbMinor = ourSuper.homeBlk.hb_minor;
			st->hbSize = ourSuper.homeBlk.hb_size;
			st->maxAlts = ourSuper.homeBlk.maxalts;
			st->defExtend = ourSuper.homeBlk.def_extend;
			st->sectorsFree = ourSuper.freeMap.sectorsFree;
			st->sectorsUsed = ourSuper.freeMap.sectorsUsed;
			st->sectorsLost = ourSuper.freeMap.sectorsLost;
			st->freeMapEntriesUsed = ourSuper.freeMap.freeMapEntriesUsed;
			st->freeMapEntriesAvail = ourSuper.freeMap.freeMapEntriesAvail;
			st->numInodesUsed = ourSuper.numInodesUsed;
			st->numInodesAvailable = ourSuper.numInodesAvailable;
			st->numDirtyInodes = ourSuper.numDirtyInodes;
			st->verbose = ourSuper.verbose;
			memset(st->bootFiles, 0, sizeof(st->bootFiles));
			if ( ourSuper.homeBlk.hb_major > 1 || ( ourSuper.homeBlk.hb_major == 1 && ourSuper.homeBlk.hb_minor >= 3 ) )
			{
				for (ii=0; ii < ourSuper.numInodesUsed; ++ii)
				{
					inode = ourSuper.inodeList[ii];
					if ( inode && (inode->flags&MGWFS_INODE_ANY_BOOT) )
					{
						int bootIdx = (inode->flags>>MGWFS_INODE_BOOT_IDX)&MGWFS_INODE_BOOT_MASK;
						buildBootFNPath(st->bootFiles[bootIdx], MAX_BOOT_FN_PATH-1, inode);
					}
				}
			}
		}
		break;
	case MGWFS_IOC_GETVERBOSE:
		*(uint32_t *)data = ourSuper.verbose;
		break;
	case MGWFS_IOC_SETVERBOSE:
		ourSuper.verbose = *(uint32_t *)data;
		break;
	case MGWFS_IOC_SETBOOT0:
	case MGWFS_IOC_SETBOOT1:
	case MGWFS_IOC_SETBOOT2:
	case MGWFS_IOC_SETBOOT3:
		if ( !options.read_write )
		{
			sts = -EROFS;
			break;
		}
		if ( ourSuper.homeBlk.hb_major > 1 || ( ourSuper.homeBlk.hb_major == 1 && ourSuper.homeBlk.hb_minor >= 3 ) )
		{
			if ( (idx = findInode(&ourSuper, FSYS_INDEX_ROOT, path)) <= 0 )
			{
				fprintf(ourSuper.logFile, "FUSE %s(): boot set returned -ENOENT because '%s' could not be found\n", Title, path);
				fflush(ourSuper.logFile);
				sts = -ENOENT;
				break;
			}
			inode = ourSuper.inodeList[idx];
			cmd -= MGWFS_IOC_SETBOOT0;
			if ( cmd && ourSuper.homeBlk.hb_major == 1 && (ourSuper.homeBlk.hb_minor < 6) )
			{
				fprintf(ourSuper.logFile, "FUSE %s(): boot set of '%s' returned -EINVAL because homeblock version of 1.%d is < 1.6\n",
						Title, path, ourSuper.homeBlk.hb_minor);
				fflush(ourSuper.logFile);
				sts = -EINVAL;
				break;
			}
			if ( ourSuper.bootIndicies[cmd] )
			{
				MgwfsInode_t *tmpInode;
				tmpInode = ourSuper.inodeList[ourSuper.bootIndicies[cmd]];
				tmpInode->flags &= ~(MGWFS_INODE_ANY_BOOT|(MGWFS_INODE_BOOT_MASK<<MGWFS_INODE_BOOT_IDX));
			}
			inode->flags &= ~(MGWFS_INODE_ANY_BOOT|(MGWFS_INODE_BOOT_MASK<<MGWFS_INODE_BOOT_IDX));
			inode->flags |= (MGWFS_INODE_ANY_BOOT|(cmd<<MGWFS_INODE_BOOT_IDX));
			ourSuper.bootIndicies[cmd] = inode->inode_no;
			switch (cmd)
			{
			case 0:
				memcpy(ourSuper.homeBlk.boot, inode->fhSectors.lba, sizeof(IndexSys_t));
				break;
			case 1:
				memcpy(ourSuper.homeBlk.boot1, inode->fhSectors.lba, sizeof(IndexSys_t));
				break;
			case 2:
				memcpy(ourSuper.homeBlk.boot2, inode->fhSectors.lba, sizeof(IndexSys_t));
				break;
			case 3:
				memcpy(ourSuper.homeBlk.boot3, inode->fhSectors.lba, sizeof(IndexSys_t));
				break;
			}
			ourSuper.specialDirtys |= SPECIAL_DIRTY_HOME;
			break;
		}
		sts = -EINVAL;
		break;
	default:
		sts = -EINVAL;
		break;
	}
	UNLOCK_IT("rdMutex", &ourSuper, &rdMutex);
	if ( (ourSuper.specialDirtys&SPECIAL_DIRTY_HOME) )
		updateAllMetaData(Title, &ourSuper);
	return sts;
}

const struct fuse_operations mgwfs_oper =
{
	.init       = mgwfs_init,
	.getattr	= mgwfs_getattr,
	.readdir	= mgwfs_readdir,
	.open		= mgwfs_open,
	.read		= mgwfs_read,
	.release	= mgwfs_release,
	.statfs		= mgwfs_statfs,
	.access		= mgwfs_access,		// int (*access) (const char *, int);
	.unlink		= mgwfs_unlink,		// int (*unlink) (const char *);
	.write		= mgwfs_write,		// int (*write) (const char *, const char *, size_t, off_t, struct fuse_file_info *);
	.flush		= mgwfs_flush,		// int (*flush) (const char *, struct fuse_file_info *);
	.fsync		= mgwfs_fsync,		// int (*fsync) (const char *, int, struct fuse_file_info *);
	.destroy	= mgwfs_destroy,	// void (*destroy) (void *private_data);
	.mkdir		= mgwfs_mkdir,		// int (*mkdir) (const char *, mode_t);
	.rmdir		= mgwfs_rmdir,		// int (*rmdir) (const char *);
	.rename		= mgwfs_rename,		// int (*rename) (const char *oldName, const char *newName, unsigned int flags);
	.create		= mgwfs_create,		// int (*create) (const char *, mode_t, struct fuse_file_info *);
	.lseek		= mgwfs_lseek,		// off_t (*lseek) (const char *, off_t off, int whence, struct fuse_file_info *);
	.truncate	= mgwfs_truncate,	// int (*truncate) (const char *, off_t, struct fuse_file_info *fi);
	.utimens	= mgwfst_utimens,	// int (*utimens) (const char *, const struct timespec tv[2], struct fuse_file_info *fi);
	.chmod		= mgwfs_chmod,		// int (*chmod) (const char *, mode_t, struct fuse_file_info *fi);
	.chown		= mgwfs_chown,		// int (*chown) (const char *, uid_t, gid_t, struct fuse_file_info *fi);
	.ioctl		= mgwfs_ioctl,		// int (*ioctl) (const char *, unsigned int cmd, void *arg, struct fuse_file_info *, unsigned int flags, void *data);
#if 0
	.fallocate	= mgwfs_fallocate,	// int (*fallocate) (const char *, int, off_t, off_t, struct fuse_file_info *);
	.read_buf	= mgwfs_read_buf,	// int (*read_buf) (const char *, struct fuse_bufvec **bufp, size_t size, off_t off, struct fuse_file_info *);
	.write_buf	= mgwfs_write_buf,	// int (*write_buf) (const char *, struct fuse_bufvec *buf, off_t off, struct fuse_file_info *);
#endif
#if 0	/* No support for these functions (yet; probably never) */
	.mknod		= mgwfs_mknod,		// int (*mknod) (const char *, mode_t, dev_t);
	.setxattr	= mgwfs_setxattr,	// int (*setxattr) (const char *, const char *, const char *, size_t, int);
	.getxattr	= mgwfs_getxattr,	// int (*getxattr) (const char *, const char *, char *, size_t);
	.listxattr	= mgwfs_listxattr,	// int (*listxattr) (const char *, char *, size_t);
	.removexattr= mgwfs_removexattr,// int (*removexattr) (const char *, const char *);
	int (*readlink) (const char *, char *, size_t);
	int (*symlink) (const char *, const char *);
	int (*link) (const char *, const char *);
	int (*opendir) (const char *, struct fuse_file_info *);
	int (*releasedir) (const char *, struct fuse_file_info *);
	int (*fsyncdir) (const char *, int, struct fuse_file_info *);
	int (*lock) (const char *, struct fuse_file_info *, int cmd,
		     struct flock *);
	int (*bmap) (const char *, size_t blocksize, uint64_t *idx);

	int (*poll) (const char *, struct fuse_file_info *,
		     struct fuse_pollhandle *ph, unsigned *reventsp);
	int (*flock) (const char *, struct fuse_file_info *, int op);
	ssize_t (*copy_file_range) (const char *path_in,
				    struct fuse_file_info *fi_in,
				    off_t offset_in, const char *path_out,
				    struct fuse_file_info *fi_out,
				    off_t offset_out, size_t size, int flags);
#endif
};


