/*
  mgwfs: Atari/MidwayGamesWest filesystem using libfuse: Filesystem in Userspace

  Copyright (C) 2025  Dave Shepperd <mgwfs@dshepperd.com>

  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.

 Build with enclosed Makefile

*/

#include "mgwfs.h"

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
		ret = -ENOENT;
	}
	else
	{
		inode = ourSuper.inodeList + idx;
		if ( S_ISDIR(inode->mode) )
		{
			stbuf->st_mode = S_IFDIR | (options.read_write ? 0775 : 0555);
			stbuf->st_nlink = 2 + inode->numInodes;
		}
		else
		{
			stbuf->st_mode = S_IFREG | (options.read_write ? 0664 : 0444);
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
	inode = ourSuper.inodeList+idx;
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
		inode = ourSuper.inodeList+idx;
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
	if ( !options.read_write && (fi->flags & (O_RDWR | O_WRONLY | O_CREAT | O_TRUNC | O_APPEND)) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_open() returned -EACCESS because '%s' is read-only.\n", path);
		fflush(ourSuper.logFile);
		return -EACCES;
	}
	do
	{
		pthread_mutex_lock(&ourSuper.ourMutex);
		idx = findInode(&ourSuper,FSYS_INDEX_ROOT,path);
		if ( !options.read_write && (fi->flags & O_CREAT) )
		{
			if ( idx )
			{
				if ( (ourSuper.verbose & VERBOSE_FUSE_CMD) )
					fprintf(ourSuper.logFile, "FUSE mgwfs_open() returned -EEXIST because '%s' (inode %d) already exists.\n", path, idx);
				retVal = -EEXIST;
				break;
			}
			// Create new file here
			// In the meantime, just error out
			fprintf(ourSuper.logFile, "FUSE mgwfs_open() for create of '%s' returned -EINVAL because file creation not coded yet.\n", path);
			retVal = -EINVAL;
			break;
		}
		if ( !idx )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_open() returned -ENOENT because '%s' could not be found\n", path);
			retVal = -ENOENT;
			break;
		}
		inode = ourSuper.inodeList+idx;
		if ( S_ISDIR(inode->mode) )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_open() returned -EINVAL because '%s' (inode %d) is a directory\n", path, idx);
			retVal = -EINVAL;
			break;
		}
		fhp = getFuseFHidx(&ourSuper, 0);
		++fhp->instances;
		fhp->inode = idx;
		fhp->offset = 0;
		fhp->readAmt = 0;
		fi->fh = fhp->index;
		if ((ourSuper.verbose&VERBOSE_FUSE_CMD))
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_open() returned success on open of '%s', inode %d and FHidx %d\n", path, idx, fhp->index);
			fflush(ourSuper.logFile);
		}
		retVal = 0;
	} while (0);
	if ( retVal )
		fflush(ourSuper.logFile);
	pthread_mutex_unlock(&ourSuper.ourMutex);
	return retVal;
}

static int mgwfs_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	MgwfsInode_t *inode;
	FuseFH_t lclFhp, *fhp=NULL;
	int cpyAmt= -EIO;
	
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_read(path='%s',buf=%p,size=%ld,offset=%ld, fi->fh=%ld\n", path, buf,size,offset,fi->fh);
		fflush(ourSuper.logFile);
	}
	do
	{
		pthread_mutex_lock(&ourSuper.ourMutex);
		if ( !fi->fh )
		{
			int idx;
			/* Didn't do an open, so just lookup the file and read it locally */
			idx = findInode(&ourSuper,FSYS_INDEX_ROOT,path);
			if (!idx)
			{
				fprintf(ourSuper.logFile, "FUSE mgwfs_read() returned -ENOENT because '%s' could not be found\n", path);
				cpyAmt = -ENOENT;
				break;
			}
			inode = ourSuper.inodeList+idx;
			if ( S_ISDIR(inode->mode) )
			{
				fprintf(ourSuper.logFile, "FUSE mgwfs_read() returned -EINVAL because '%s' (inode %d) is a directory\n", path, idx);
				cpyAmt = -EINVAL;
				break;
			}
			fhp = &lclFhp;
			fhp->index = 1;
			fhp->buffer = NULL;
			fhp->inode = idx;
			fhp->instances = 1;
			fhp->offset = 0;
			fhp->readAmt = 0;
		}
		else
		{
			fhp = getFuseFHidx(&ourSuper,fi->fh);
			inode = ourSuper.inodeList + fhp->inode;
		}
		if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_read('%s'): fhp=%s, fhp->readAmt=%d, offset=%d, bsize=%d\n",
					path,
					fhp != &lclFhp ? "allocated":"local",
					fhp->readAmt,
					fhp->offset,
					fhp->bufferSize
					);
			fflush(ourSuper.logFile);
		}
		if ( offset >= inode->fsHeader.size )
		{
			fhp->offset = offset;
			cpyAmt = 0;
			break;
		}
		if ( !fhp->readAmt || !fhp->buffer || fhp->bufferSize < inode->fsHeader.size )
		{
			if ( fhp->buffer )
				free(fhp->buffer);
			fhp->bufferSize = inode->fsHeader.size;
			fhp->buffer = (uint8_t *)malloc(inode->fsHeader.size);
			/* Read the whole file into a local buffer */
			fhp->readAmt = readFile("mgwfs_read():", &ourSuper, fhp->buffer, inode->fsHeader.size, inode->fsHeader.pointers[0]);
			if ( fhp->readAmt < 0 )
			{
				fprintf(ourSuper.logFile, "FUSE mgwfs_read('%s') readFile() returned error %d. offset=%ld\n", path, fhp->readAmt, offset );
				cpyAmt = fhp->readAmt;
				break;
			}
		}
		if ( fhp->readAmt > 0 )
		{
			cpyAmt = size;
			if ( cpyAmt+offset > (off_t)inode->fsHeader.size )
				cpyAmt = inode->fsHeader.size-offset;
			if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
			{
				fprintf(ourSuper.logFile, "FUSE mgwfs_read('%s') copying %d bytes. offset=%ld, readAmt=%d\n", path, cpyAmt, offset, fhp->readAmt );
			}
			memcpy(buf,fhp->buffer+offset,cpyAmt);
			fhp->offset += size;
		}
		else
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_read('%s') readFile() failed with %d\n", path, fhp->readAmt );
		}
		if ( fhp == &lclFhp && fhp->buffer )
		{
			free(fhp->buffer);
			fhp->buffer = NULL;
		}
	} while (0);
	fflush(ourSuper.logFile);
	pthread_mutex_unlock(&ourSuper.ourMutex);
	return cpyAmt;
}

static int mgwfs_release(const char *path, struct fuse_file_info *fi)
{
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
			if ( (fi->flags&(O_APPEND|O_CREAT|O_RDWR|O_RDWR|O_TRUNC)) )
			{
			}
			freeFuseFHidx(&ourSuper, fi->fh);
			fi->fh = 0;
		}
		pthread_mutex_unlock(&ourSuper.ourMutex);
	}
	return 0;
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
	int retVal= -ENOENT, idx;
	MgwfsSuper_t *super = &ourSuper;
	FsysRetPtr tmp;
	uint32_t *indexPtr;
	
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
		curr = super->inodeList+idx;
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
		/* Get pointer to previous inode if there is one */
		if ( curr->idxPrevInode )
			prev = super->inodeList+curr->idxPrevInode;
		/* Get pointer to next inode if there is one */
		if ( curr->idxNextInode )
			next = super->inodeList+curr->idxNextInode;
		/* If there's no previous, then point to the parent */
		if ( !prev )
			parent = super->inodeList + curr->idxParentInode;
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
		for (ii=0; ii < FSYS_MAX_ALTS; ++ii)
		{
			if ( ((tmp.start = indexPtr[ii])&FSYS_LBA_MASK) )
			{
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
				mgwfsFreeSectors(super, NULL, rp);
			}
		}
	} while ( 0 );
	fflush(super->logFile);
	pthread_mutex_unlock(&super->ourMutex);
	return retVal;
}

static int mgwfs_write (const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	MgwfsInode_t *inode;
	FuseFH_t lclFhp, *fhp=NULL;
	int cpyAmt= -EIO;

	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfs_write(path='%s',buf=%p,size=%ld,offset=%ld, fi->fh=%ld\n", path, buf, size, offset, fi->fh );
		fflush(ourSuper.logFile);
	}
	do
	{
		pthread_mutex_lock(&ourSuper.ourMutex);
		if ( !fi->fh )
		{
			int idx;
			/* Didn't do an open, so just lookup the file and write it locally */
			idx = findInode(&ourSuper,FSYS_INDEX_ROOT,path);
			if (!idx)
			{
				fprintf(ourSuper.logFile, "FUSE mgwfs_write() returned -ENOENT because '%s' could not be found\n", path);
				cpyAmt = -ENOENT;
				break;
			}
			inode = ourSuper.inodeList+idx;
			if ( S_ISDIR(inode->mode) )
			{
				fprintf(ourSuper.logFile, "FUSE mgwfs_write() returned -EINVAL because '%s' (inode %d) is a directory\n", path, idx);
				cpyAmt = -EINVAL;
				break;
			}
			fhp = &lclFhp;
			fhp->index = 1;
			fhp->buffer = NULL;
			fhp->inode = idx;
			fhp->instances = 1;
			fhp->offset = 0;
			fhp->readAmt = 0;
		}
		else
		{
			fhp = getFuseFHidx(&ourSuper,fi->fh);
			inode = ourSuper.inodeList + fhp->inode;
		}
		if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfs_write('%s'): fhp=%s, fhp->readAmt=%d, offset=%d, bsize=%d\n",
					path,
					fhp != &lclFhp ? "allocated":"local",
					fhp->readAmt,
					fhp->offset,
					fhp->bufferSize
					);
			fflush(ourSuper.logFile);
		}
		// Write this function
	} while (0);
	pthread_mutex_unlock(&ourSuper.ourMutex);
	fflush(ourSuper.logFile);
	return cpyAmt;
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
	.access		= mgwfs_access,	// int (*access) (const char *, int);
	.unlink		= mgwfs_unlink,	// int (*unlink) (const char *);
	.write		= mgwfs_write,		// int (*write) (const char *, const char *, size_t, off_t, struct fuse_file_info *);
#if 0
	.mkdir		= mgwfs_mkdir,		// int (*mkdir) (const char *, mode_t);
	.rmdir		= mgwfs_rmdir,		// int (*rmdir) (const char *);
	.rename		= mgwfs_rename,	// int (*rename) (const char *, const char *, unsigned int flags);
	.create		= mgwfs_create,	// int (*create) (const char *, mode_t, struct fuse_file_info *);
	.truncate	= mgwfs_truncate,	// int (*truncate) (const char *, off_t, struct fuse_file_info *fi);
	.lseek		= mgwfs_lseek,		// off_t (*lseek) (const char *, off_t off, int whence, struct fuse_file_info *);
	.write_buf	= mgwfs_write_buf,	// int (*write_buf) (const char *, struct fuse_bufvec *buf, off_t off, struct fuse_file_info *);
	.flush		= mgwfs_flush,		// int (*flush) (const char *, struct fuse_file_info *);
	.fsync		= mgwfs_fsync,		// int (*fsync) (const char *, int, struct fuse_file_info *);
	.fallocate	= mgwfs_fallocate,	// int (*fallocate) (const char *, int, off_t, off_t, struct fuse_file_info *);
#endif
#if 0
	.read_buf	= mgwfs_read_buf,	// int (*read_buf) (const char *, struct fuse_bufvec **bufp, size_t size, off_t off, struct fuse_file_info *);
	.destroy	= mgwfs_destroy,	// void (*destroy) (void *private_data);
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


