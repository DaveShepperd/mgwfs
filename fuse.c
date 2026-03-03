/*
  mgwfs: Atari/MidwayGamesWest filesystem using libfuse: Filesystem in Userspace

  Copyright (C) 2025  Dave Shepperd <mgwfs@dshepperd.com>

  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.

 Build with enclosed Makefile

*/

#include "mgwfs.h"

static void clearDirty(MgwfsSuper_t *super)
{
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs clearDirty(). Would have written back to disk %d inodes\n", super->numDirtyInodes);
		fflush(ourSuper.logFile);
	}
	super->numDirtyInodes = 0;
}

void mgwfsAddToDirty(MgwfsSuper_t *super, int idx)
{
	if ( idx )
	{
		if ( super->numDirtyInodes >= MAX_DIRTY_INODE )
			clearDirty(super);
		super->dirtyInodes[super->numDirtyInodes] = idx;
		if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfsAddToDirty(). Added inode %d to dirtyInode[%d]\n",
					idx, super->numDirtyInodes);
			fflush(ourSuper.logFile);
		}
		++super->numDirtyInodes;
	}
}

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
	pthread_mutex_lock(&ourSuper.ourMutex);
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
	pthread_mutex_unlock(&ourSuper.ourMutex);
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
	pthread_mutex_lock(&ourSuper.ourMutex);
	idx = findInode(&ourSuper,FSYS_INDEX_ROOT,path);
	if (!idx)
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_readdir() returned -ENOENT because '%s' could not be found\n", path);
		fflush(ourSuper.logFile);
		pthread_mutex_unlock(&ourSuper.ourMutex);
		return -ENOENT;
	}
	inode = ourSuper.inodeList[idx];
	if ( !S_ISDIR(inode->mode) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_readdir() returned -ENOENT because '%s' (inode %d) is not a directory\n", path, idx);
		fflush(ourSuper.logFile);
		pthread_mutex_unlock(&ourSuper.ourMutex);
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
	pthread_mutex_unlock(&ourSuper.ourMutex);
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
#if 0
	if ( !options.read_write && (fi->flags & (O_RDWR | O_TRUNC | O_APPEND | O_WRONLY | O_CREAT )) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_open() returned -EACCESS because '%s' is mounted read-only.\n", path);
		fflush(ourSuper.logFile);
		return -EACCES;
	}
#endif
	do
	{
		pthread_mutex_lock(&ourSuper.ourMutex);
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
		fhp->rwBuffSize = inode->fsHeader.clusters*BYTES_PER_SECTOR;
		fhp->rwBuff = (uint8_t *)malloc(fhp->rwBuffSize);
		/* Read the whole file into a local buffer */
		fhp->rwBuffErr = readWholeFile("FUSE mgwfs_open():", &ourSuper, fhp->rwBuff, inode->fsHeader.size, inode->fsHeader.pointers[0]);
		if ( fhp->rwBuffErr >= 0 )
		{
			fhp->rwBuffUsed = fhp->rwBuffErr;
			if ( (fi->flags&(O_WRONLY|O_RDWR)) )
			{
				if ( (fi->flags & O_TRUNC) )
					fhp->rwBuffUsed = 0;
				if ( (fi->flags & O_APPEND) )
					fhp->rwBuffOffset = fhp->rwBuffUsed;
			}
			if ((ourSuper.verbose&VERBOSE_FUSE_CMD))
			{
				fprintf(ourSuper.logFile, "FUSE mgwfs_open(%s,0x%X) returned success on open, inode %d and FHidx %d, rwBuff=%p, rwBuffUsed=%d, rwBuffOffset=%ld, rwBuffSize=%d\n"
						,path
						,fhp->openFlags
						,idx
						,fhp->index
						,fhp->rwBuff
						,fhp->rwBuffUsed
						,fhp->rwBuffOffset
						,fhp->rwBuffSize
						 );
			}
		}
		else
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_open('%s') readFile() returned error %d.\n", path, fhp->rwBuffErr );
		}
	} while (0);
	fflush(ourSuper.logFile);
	pthread_mutex_unlock(&ourSuper.ourMutex);
	return retVal;
}

#if 0
static int readLocked(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	MgwfsInode_t *inode;
	FuseFH_t lclFhp, *fhp=NULL;
	int cpyAmt= -EIO;

	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE readLocked(path='%s',buf=%p,size=%ld,offset=%ld, fi->fh=%ld\n", path, buf,size,offset,fi->fh);
		fflush(ourSuper.logFile);
	}
	do
	{
		if ( !fi->fh )
		{
			int idx;
			/* Didn't do an open, so just lookup the file and read it locally */
			idx = findInode(&ourSuper,FSYS_INDEX_ROOT,path);
			if (!idx)
			{
				fprintf(ourSuper.logFile, "FUSE readLocked() returned -ENOENT because '%s' could not be found\n", path);
				cpyAmt = -ENOENT;
				break;
			}
			inode = ourSuper.inodeList[idx];
			if ( S_ISDIR(inode->mode) )
			{
				fprintf(ourSuper.logFile, "FUSE readLocked() returned -EINVAL because '%s' (inode %d) is a directory\n", path, idx);
				cpyAmt = -EINVAL;
				break;
			}
			fhp = &lclFhp;
			memset(fhp,0,sizeof(lclFhp));
			fhp->index = 1;
			fhp->inode = idx;
			fhp->instances = 1;
			fhp->openFlags = O_RDONLY;
		}
		else
		{
			fhp = getFuseFHidx(&ourSuper,fi->fh);
			inode = ourSuper.inodeList[fhp->inode];
		}
		if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
		{
			fprintf(ourSuper.logFile, "FUSE readLocked('%s'): fhp=%s, fhp->readAmt=%d, offset=%ld, bsize=%d\n"
					,path
					,fhp != &lclFhp ? "allocated":"local"
					,fhp->rwBuffUsed
					,fhp->rwBuffOffset
					,fhp->rwBuffSize
					);
			fflush(ourSuper.logFile);
		}
		if ( offset >= inode->fsHeader.size )
		{
			fhp->rwBuffOffset = offset;
			cpyAmt = 0;
			break;
		}
		if ( !fhp->rwBuffUsed || !fhp->rwBuff || fhp->rwBuffSize < inode->fsHeader.size )
		{
			if ( fhp->rwBuff )
				free(fhp->rwBuff);
			fhp->rwBuffSize = inode->fsHeader.size;
			fhp->rwBuff = (uint8_t *)malloc(inode->fsHeader.size);
			/* Read the whole file into a local buffer */
			fhp->rwBuffUsed = readFile("FUSE readLocked():", &ourSuper, fhp->rwBuff, inode->fsHeader.size, inode->fsHeader.pointers[0]);
			if ( fhp->rwBuffUsed < 0 )
			{
				fprintf(ourSuper.logFile, "FUSE readLocked('%s') readFile() returned error %d. offset=%ld\n", path, fhp->rwBuffUsed, offset );
				cpyAmt = fhp->rwBuffUsed;
				break;
			}
		}
		if ( fhp->rwBuffUsed > 0 )
		{
			cpyAmt = size;
			if ( cpyAmt+offset > (off_t)inode->fsHeader.size )
				cpyAmt = inode->fsHeader.size-offset;
			if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
			{
				fprintf(ourSuper.logFile, "FUSE readLocked('%s') copying %d bytes. offset=%ld, readAmt=%d\n", path, cpyAmt, offset, fhp->rwBuffUsed );
			}
			memcpy(buf,fhp->rwBuff+offset,cpyAmt);
			fhp->rwBuffOffset += size;
		}
		else
		{
			fprintf(ourSuper.logFile, "FUSE readLocked('%s') readFile() failed with %d\n", path, fhp->rwBuffUsed );
		}
		if ( fhp == &lclFhp && fhp->rwBuff )
		{
			free(fhp->rwBuff);
			fhp->rwBuff = NULL;
		}
	} while (0);
	fflush(ourSuper.logFile);
	return cpyAmt;
}
#endif

static int mgwfs_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	int retVal = -EIO;
	FuseFH_t *fhp;
	MgwfsInode_t *inode;
	
	pthread_mutex_lock(&ourSuper.ourMutex);
	do
	{
		size_t cpyAmt;

		fhp = getFuseFHidx(&ourSuper, fi->fh);
		if ( !fhp )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_read('%s', %ld, 0x%lX): not opened. Returned -EIO\n"
					,path
					,size
					,offset
					);
			break;
		}
		if ( fhp->rwBuffErr < 0 )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_read('%s', %ld, 0x%lX): Found rwBuffErr=%d\n"
					,path
					,size
					,offset
					,fhp->rwBuffErr
					);
			retVal = fhp->rwBuffErr;
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
					,fhp->rwBuffUsed
					,fhp->rwBuffOffset
					,fhp->rwBuffSize
					,fhp->rwBuffErr
					);
			fflush(ourSuper.logFile);
		}
		cpyAmt = 0;
		if ( fhp->rwBuffUsed > 0 )
		{
			off_t adjOffset = offset;
			cpyAmt = size;
			if ( adjOffset > fhp->rwBuffUsed )
				adjOffset = fhp->rwBuffUsed;
			if ( cpyAmt + adjOffset > fhp->rwBuffUsed )
				cpyAmt = fhp->rwBuffUsed-adjOffset;
			if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
			{
				fprintf(ourSuper.logFile, "FUSE mgwfs_read('%s', %ld, 0x%lX) cpyAmt=%ld, adjOffset=%ld, rwBuffUsed=%d\n",
						path,
						size,
						offset,
						cpyAmt,
						adjOffset,
						fhp->rwBuffUsed );
			}
			if ( cpyAmt > 0 )
			{
				memcpy(buf, fhp->rwBuff + adjOffset, cpyAmt);
				fhp->rwBuffOffset += cpyAmt;
			}
		}
		retVal = cpyAmt;
	} while (0);
	if ( retVal < 0 )
		fflush(ourSuper.logFile);
	pthread_mutex_unlock(&ourSuper.ourMutex);
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
		
		pthread_mutex_lock(&ourSuper.ourMutex);
		fhp = getFuseFHidx(&ourSuper,fi->fh);
		if ( --fhp->instances <= 0 )
		{
			sts = fileClose("mgwfs_release():",&ourSuper,fhp);
			freeFuseFHidx(&ourSuper, fi->fh);
			fi->fh = 0;
		}
		pthread_mutex_unlock(&ourSuper.ourMutex);
	}
	return sts;
}

/* Should be set to BYTES_PER_SECTOR, but it doesn't like that */
#define BLOCK_SIZE (4096)

static int mgwfs_statfs(const char *path, struct statvfs *stp)
{
	FsysRetPtr *rp;
	
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_statfs('%s',%p\n", path, stp);
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
	
	pthread_mutex_lock(&ourSuper.ourMutex);
	idx = findInode(&ourSuper,FSYS_INDEX_ROOT,path);
	pthread_mutex_unlock(&ourSuper.ourMutex);
	return idx ? 0 : -ENOENT;
}

static int mgwfs_unlink(const char *path)
{
	int verbLen,retVal= -ENOENT, idx;
	MgwfsSuper_t *super = &ourSuper;
	FsysRetPtr tmp;
	uint32_t *indexPtr;
	char verbBuff[200];
	
	pthread_mutex_lock(&super->ourMutex);
	do
	{
		MgwfsInode_t *parent, *prev, *curr, *next;
		FsysRetPtr *rp;
		int ii, jj;
		
		if ( (super->verbose&VERBOSE_FUSE_CMD) )
		{
			fprintf(super->logFile, "FUSE mgwfs_unlink('%s')\n", path );
			fflush(super->logFile);
		}
		idx = findInode(&ourSuper,FSYS_INDEX_ROOT,path);
		if ( !idx )
		{
			if ( (super->verbose&VERBOSE_FUSE_CMD) )
				fprintf(super->logFile, "FUSE mgwfs_unlink('%s') returned ENOENT\n", path );
			retVal = -ENOENT;
			break;
		}
		curr = super->inodeList[idx];
		if ( S_ISDIR(curr->mode) )
		{
			if ( (super->verbose&VERBOSE_FUSE_CMD) )
				fprintf(super->logFile, "FUSE mgwfs_unlink() returned -EINVAL because '%s' (inode %d) is a directory\n", path, idx);
			retVal = -EINVAL;
			break;
		}
		// Need to remove the filename from the directory to which this file is listed
		/* Assume no pointers */
		parent = NULL;
		prev = NULL;
		next = NULL;
		mgwfsAddToDirty(super,curr->idxParentInode);
		/* Get pointer to previous inode if there is one */
		if ( curr->idxPrevInode )
			prev = super->inodeList[curr->idxPrevInode];
		/* Get pointer to next inode if there is one */
		if ( curr->idxNextInode )
			next = super->inodeList[+curr->idxNextInode];
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
			fprintf(super->logFile, "FUSE mgwfs_unlink('%s') returned -EIO because (inode %d) has no parent entry\n", path, idx);
			fprintf(super->errFile, "FUSE mgwfs_unlink('%s') returned -EIO because (inode %d) has no parent entry\n", path, idx);
			retVal = -EIO;
			break;
		}
		tmp.nblocks = 1;
		// Need to free the sectors assigned to the file headers assigned to this file
		indexPtr = super->indexSys+(curr->inode_no*FSYS_MAX_ALTS);
		verbLen = 0;
		if ( (super->verbose&VERBOSE_FUSE_CMD) )
		{
			verbLen = snprintf(verbBuff,sizeof(verbBuff), "FUSE mgwfs_unlink('%s'): free sectors ",
					path);
		}
		for (ii=0; ii < FSYS_MAX_ALTS; ++ii)
		{
			if ( ((tmp.start = indexPtr[ii])&FSYS_LBA_MASK) )
			{
				if ( verbLen )
				{
					verbLen += snprintf(verbBuff+verbLen,sizeof(verbBuff)-verbLen,
									 " 0x%08X/%d",
										tmp.start,
										tmp.nblocks);
				}
				mgwfsFreeSectors(super,NULL,&tmp);
				// Need to mark the entries in index.sys as available
				indexPtr[ii] = FSYS_EMPTYLBA_BIT;
			}
		}
		super->indexSysDirty = 1;	/* index.sys is dirty now */
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
				mgwfsFreeSectors(super, NULL, rp);
			}
		}
		if ( verbLen )
			fprintf(super->logFile, "%s\n", verbBuff);
		super->inodeList[idx] = NULL;
		free(curr);
	} while ( 0 );
	fflush(super->logFile);
	pthread_mutex_unlock(&super->ourMutex);
	return retVal;
}

static off_t addToBuff(FuseFH_t *fhp, const char *path, off_t off)
{
	if ( off >= fhp->rwBuffSize )
	{
		uint8_t *newPtr;
		int sectors = (off+BYTES_PER_SECTOR-1)/BYTES_PER_SECTOR;
		int bytes = sectors*BYTES_PER_SECTOR;
		newPtr = (uint8_t *)realloc(fhp->rwBuff, bytes);
		if ( !newPtr )
		{
			fprintf(ourSuper.logFile, "FUSE addToBuff(%s,0x%lX). Out of memory to allocate %d bytes.\n", path, off, bytes );
			return -ENOMEM;
		}
		fhp->rwBuff = newPtr;
		fhp->rwBuffSize = bytes;
		memset(fhp->rwBuff+fhp->rwBuffUsed,0,fhp->rwBuffSize-fhp->rwBuffUsed);
	}
	if ( off > fhp->rwBuffUsed )
		fhp->rwBuffUsed = off;
	fhp->rwBuffOffset = off;
	return off;
}

static int mgwfs_write (const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	MgwfsInode_t *inode;
	FuseFH_t *fhp=NULL;
	int cpyAmt= -EIO;
	
#if 0
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_write('%s',%p,%ld,0x%lX,%ld)\n", path, buf, size, offset, fi->fh );
		fflush(ourSuper.logFile);
	}
#endif
	do
	{
		pthread_mutex_lock(&ourSuper.ourMutex);
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
					,fhp->rwBuff
					,fhp->rwBuffUsed
					,fhp->rwBuffOffset
					,fhp->rwBuffSize
					);
			fflush(ourSuper.logFile);
		}
		if ( size + offset > fhp->rwBuffSize )
		{
			off_t currOffset = fhp->rwBuffOffset;
			cpyAmt = addToBuff(fhp,path,offset+size);
			if ( cpyAmt < 0 )
				break;
			fhp->rwBuffOffset = currOffset;
		}
		cpyAmt = size;
		memcpy(fhp->rwBuff + fhp->rwBuffOffset, buf, cpyAmt);
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
					,fhp->rwBuffOffset
					);
			fflush(ourSuper.logFile);
		}
		fhp->rwBuffOffset += cpyAmt;
		if ( fhp->rwBuffOffset > fhp->rwBuffUsed )
			fhp->rwBuffUsed = fhp->rwBuffOffset;
		if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_write('%s',%p,%ld,0x%lX,%ld): After:  rwBuff=%p, rwBuffUsed=%d, rwBuffOffset=%ld, rwBuffSize=%d\n"
					,path
					,buf
					,size
					,offset
					,fi->fh
					,fhp->rwBuff
					,fhp->rwBuffUsed
					,fhp->rwBuffOffset
					,fhp->rwBuffSize
					);
			fflush(ourSuper.logFile);
		}
	} while (0);
	pthread_mutex_unlock(&ourSuper.ourMutex);
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
	pthread_mutex_lock(&ourSuper.ourMutex);
	fhp = getFuseFHidx(&ourSuper,fi->fh);
	if ( fhp )
	{
		sts = fileFlush("mgwfs_flush()", &ourSuper, fhp);
		if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_flush('%s',%ld): rwBuff=%p, rwBuffUsed=%d, rwBuffOffset=%ld, rwBuffSize=%d\n"
					,path
					,fi->fh
					,fhp->rwBuff
					,fhp->rwBuffUsed
					,fhp->rwBuffOffset
					,fhp->rwBuffSize
					);
			fflush(ourSuper.logFile);
		}
	}
	else
		sts = -EIO;
	pthread_mutex_unlock(&ourSuper.ourMutex);
	return sts;
}

static int mgwfs_fsync(const char *path, int arg, struct fuse_file_info *fi)
{
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_fsync(path='%s', fi->fh=%ld\n", path, fi->fh );
		fflush(ourSuper.logFile);
	}
	return -EIO;
}

static void mgwfs_destroy(void *private_data)
{
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_destroy(), pd=%p, &ourSuper=%p\n", private_data, &ourSuper );
		fflush(ourSuper.logFile);
	}
	mgwfs_fsync("/",0,NULL);
}

// Flags can be one of:
//RENAME_NOREPLACE is set and the target exists, return an error (e.g., EEXIST).
//RENAME_EXCHANGE is set, both files must exist, and they are swapped
static int mgwfs_rename (const char *oldName, const char *newName, unsigned int flags)
{
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_rename('%s','%s',0x%X)\n", oldName, newName, flags );
		fflush(ourSuper.logFile);
	}
	return options.read_write ? -EPERM : -EEXIST;
}

static int mgwfs_mkdir(const char *path, mode_t mode)
{
	int sts = options.read_write ? -EPERM : -EIO;
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_mkdir('%s',0x%X), sts=%d\n", path, mode, sts);
		fflush(ourSuper.logFile);
	}
	return sts;
}

static int mgwfs_rmdir(const char *path)
{
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_rmdir('%s')\n", path);
		fflush(ourSuper.logFile);
	}
	return options.read_write ? -EPERM : -EIO;
}

static int mgwfs_create(const char *path, mode_t fMode, struct fuse_file_info *fi)
{
	int idx, sts=0;
	idx = fileCreate("mgwfs_open()", path, &ourSuper);
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
	if ( off < 0 )
		off = 0;
	if ( off <= fhp->rwBuffUsed )
	{
		fhp->rwBuffOffset = off;
		return off;
	}
	if ( !(fhp->openFlags & (O_RDWR | O_WRONLY)) )
	{
		/* read only, cannot go past EOF */
		if ( off >= fhp->rwBuffUsed )
			off = fhp->rwBuffUsed;
		fhp->rwBuffOffset = off;
		return off;
	}
	/* r/w or wo, maybe add to end of file */
	return addToBuff(fhp,path,off);
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

		fhp = getFuseFHidx(&ourSuper,fi->fh);
		if ( fhp )
		{
			switch (whence)
			{
			case SEEK_SET:
				sts = moveOffset(path,fhp,off);
				break;

			case SEEK_CUR:
				newOff = fhp->rwBuffOffset+off;
				sts = moveOffset(path,fhp,newOff);
				break;

			case SEEK_END:
				newOff = fhp->rwBuffUsed+off;
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
	pthread_mutex_lock(&ourSuper.ourMutex);
	sts = lseek_locked(path,off,whence,fi);
	pthread_mutex_unlock(&ourSuper.ourMutex);
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
		
		pthread_mutex_lock(&ourSuper.ourMutex);
		fhp = getFuseFHidx(&ourSuper, fi->fh);
		if ( (fhp->openFlags & (O_RDWR|O_WRONLY)) )
		{
			sts = moveOffset(path,fhp,offset);
			if ( sts >= 0 )
			{
				fhp->rwBuffUsed = offset;
				fhp->rwBuffOffset = offset;
			}
		}
		pthread_mutex_unlock(&ourSuper.ourMutex);
	}
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
#if 0
	.fallocate	= mgwfs_fallocate,	// int (*fallocate) (const char *, int, off_t, off_t, struct fuse_file_info *);
#endif
#if 0
	.read_buf	= mgwfs_read_buf,	// int (*read_buf) (const char *, struct fuse_bufvec **bufp, size_t size, off_t off, struct fuse_file_info *);
	.write_buf	= mgwfs_write_buf,	// int (*write_buf) (const char *, struct fuse_bufvec *buf, off_t off, struct fuse_file_info *);
	.utimens	= mgwfst_utimens,	// int (*utimens) (const char *, const struct timespec tv[2], struct fuse_file_info *fi);
#endif
#if 0	/* No support for these functions (yet; probably never) */
	.chmod		= mgwfs_chmod,		// int (*chmod) (const char *, mode_t, struct fuse_file_info *fi);
	.chown		= mgwfs_chown,		// int (*chown) (const char *, uid_t, gid_t, struct fuse_file_info *fi);
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

	int (*ioctl) (const char *, unsigned int cmd, void *arg,
		      struct fuse_file_info *, unsigned int flags, void *data);
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


