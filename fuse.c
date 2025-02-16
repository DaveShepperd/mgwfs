/*
  mgwfsf: Atari/MidwayGamesWest filesystem using libfuse: Filesystem in Userspace

  Copyright (C) 2025  Dave Shepperd <mgwfsf@dshepperd.com>

  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.

 Build with enclosed Makefile

*/

#include "mgwfsf.h"

/* Not sure if a mutex is needed, but just to be safe we use one to force single threading */
static pthread_mutex_t ourMutex = PTHREAD_MUTEX_INITIALIZER;

static void *mgwfsf_init(struct fuse_conn_info *conn,
			struct fuse_config *cfg)
{
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfsf_init()\n");
		fflush(ourSuper.logFile);
	}
	cfg->kernel_cache = 1;
	return NULL;
}

static int mgwfsf_getattr(const char *path, struct stat *stbuf,
			 struct fuse_file_info *fi)
{
	int idx, ret=0;
	MgwfsInode_t *inode;
	
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfsf_getattr(path='%s',stbuf)\n", path);
		fflush(ourSuper.logFile);
	}
	memset(stbuf, 0, sizeof(struct stat));
	pthread_mutex_lock(&ourMutex);
	if ( (idx = findInode(&ourSuper, FSYS_INDEX_ROOT, path)) <= 0 )
	{
		ret = -ENOENT;
	}
	else
	{
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
	}
	pthread_mutex_unlock(&ourMutex);
	return ret;
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
	pthread_mutex_lock(&ourMutex);
	idx = findInode(&ourSuper,FSYS_INDEX_ROOT,path);
	if (!idx)
	{
		fprintf(ourSuper.logFile, "FUSE mgwfsf_readdir() returned -ENOENT because '%s' could not be found\n", path);
		fflush(ourSuper.logFile);
		pthread_mutex_unlock(&ourMutex);
		return -ENOENT;
	}
	inode = ourSuper.inodeList+idx;
	if ( !S_ISDIR(inode->mode) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfsf_readdir() returned -ENOENT because '%s' (inode %d) is not a directory\n", path, idx);
		fflush(ourSuper.logFile);
		pthread_mutex_unlock(&ourMutex);
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
		fRet = filler(buf, inode->fileName, &stbuf, 0, FUSE_FILL_DIR_PLUS);
		if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfsf_readdir(): Uploaded inode %d '%s'. Next=%d. fRet=%d\n", idx, inode->fileName, inode->idxNextInode, fRet );
			fflush(ourSuper.logFile);
		}
		if ( fRet )
			break;
		idx = inode->idxNextInode;
	}
	pthread_mutex_unlock(&ourMutex);
	return 0;
}

static int mgwfsf_open(const char *path, struct fuse_file_info *fi)
{
	MgwfsInode_t *inode;
	FuseFH_t *fhp;
	int idx;

	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
		fprintf(ourSuper.logFile, "FUSE mgwfsf_open(path='%s',fi->fh=%ld)\n", path, fi->fh);
	pthread_mutex_lock(&ourMutex);
	idx = findInode(&ourSuper,FSYS_INDEX_ROOT,path);
	if (!idx)
	{
		fprintf(ourSuper.logFile, "FUSE mgwfsf_open() returned -ENOENT because '%s' could not be found\n", path);
		fflush(ourSuper.logFile);
		pthread_mutex_unlock(&ourMutex);
		return -ENOENT;
	}
	inode = ourSuper.inodeList+idx;
	if ( S_ISDIR(inode->mode) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfsf_open() returned -EINVAL because '%s' (inode %d) is a directory\n", path, idx);
		fflush(ourSuper.logFile);
		pthread_mutex_unlock(&ourMutex);
		return -EINVAL;
	}
	if ((fi->flags & O_ACCMODE) != O_RDONLY)
	{
		fprintf(ourSuper.logFile, "FUSE mgwfsf_open() returned -EACCESS because '%s' (inode %d) is read-only. Access flags=0x%X\n", path, idx, fi->flags);
		fflush(ourSuper.logFile);
		pthread_mutex_unlock(&ourMutex);
		return -EACCES;
	}
	fhp = getFuseFHidx(&ourSuper, 0);
	++fhp->instances;
	fhp->inode = idx;
	fhp->offset = 0;
	fhp->readAmt = 0;
	fi->fh = fhp->index;
	if ((ourSuper.verbose&VERBOSE_FUSE_CMD))
	{
		fprintf(ourSuper.logFile, "FUSE mgwfsf_open() returned success on open of '%s', inode %d and FHidx %d\n", path, idx, fhp->index);
		fflush(ourSuper.logFile);
	}
	pthread_mutex_unlock(&ourMutex);
	return 0;
}

static int mgwfsf_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	MgwfsInode_t *inode;
	FuseFH_t lclFhp, *fhp=NULL;
	int cpyAmt= -EIO;
	
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfsf_read(path='%s',buf=%p,size=%ld,offset=%ld, fi->fh=%ld\n", path, buf,size,offset,fi->fh);
		fflush(ourSuper.logFile);
	}
	pthread_mutex_lock(&ourMutex);
	if ( !fi->fh )
	{
		int idx;
		/* Didn't do an open, so just lookup the file and read it locally */
		idx = findInode(&ourSuper,FSYS_INDEX_ROOT,path);
		if (!idx)
		{
			fprintf(ourSuper.logFile, "FUSE mgwfsf_open() returned -ENOENT because '%s' could not be found\n", path);
			fflush(ourSuper.logFile);
			pthread_mutex_unlock(&ourMutex);
			return -ENOENT;
		}
		inode = ourSuper.inodeList+idx;
		if ( S_ISDIR(inode->mode) )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfsf_open() returned -EINVAL because '%s' (inode %d) is a directory\n", path, idx);
			fflush(ourSuper.logFile);
			pthread_mutex_unlock(&ourMutex);
			return -EINVAL;
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
		fprintf(ourSuper.logFile, "FUSE mgwfsf_read('%s'): fhp=%s, fhp->readAmt=%d, offset=%d, bsize=%d\n",
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
		pthread_mutex_unlock(&ourMutex);
		return 0;
	}
	if ( !fhp->readAmt || !fhp->buffer || fhp->bufferSize < inode->fsHeader.size )
	{
		if ( fhp->buffer )
			free(fhp->buffer);
		fhp->bufferSize = inode->fsHeader.size;
		fhp->buffer = (uint8_t *)malloc(inode->fsHeader.size);
		fhp->readAmt = readFile("mgwfsf_read():", &ourSuper, fhp->buffer, inode->fsHeader.size, inode->fsHeader.pointers[0]);
		if ( fhp->readAmt < 0 )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfsf_read('%s') readFile() returned error %d. offset=%ld\n", path, fhp->readAmt, offset );
			fflush(ourSuper.logFile);
			pthread_mutex_unlock(&ourMutex);
			return fhp->readAmt;
		}
	}
	if ( fhp->readAmt > 0 )
	{
		cpyAmt = size;
		if ( cpyAmt+offset > (off_t)inode->fsHeader.size )
			cpyAmt = inode->fsHeader.size-offset;
		if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
		{
			fprintf(ourSuper.logFile, "FUSE mgwfsf_read('%s') copying %d bytes. offset=%ld, readAmt=%d\n", path, cpyAmt, offset, fhp->readAmt );
			fflush(ourSuper.logFile);
		}
		memcpy(buf,fhp->buffer+offset,cpyAmt);
		fhp->offset += size;
	}
	else
	{
		fprintf(ourSuper.logFile, "FUSE mgwfsf_read('%s') readFile() failed with %d\n", path, fhp->readAmt );
		fflush(ourSuper.logFile);
	}
	if ( fhp == &lclFhp && fhp->buffer )
		free(fhp->buffer);
	pthread_mutex_unlock(&ourMutex);
	return cpyAmt;
}

int mgwfsf_release(const char *path, struct fuse_file_info *fi)
{
	if ( (ourSuper.verbose&VERBOSE_FUSE_CMD) )
	{
		fprintf(ourSuper.logFile, "FUSE mgwfsf_release(path='%s',fi->fh=%ld\n", path, fi->fh);
		fflush(ourSuper.logFile);
	}
	if ( fi->fh )
	{
		FuseFH_t *fhp;
		
		pthread_mutex_lock(&ourMutex);
		fhp = getFuseFHidx(&ourSuper,fi->fh);
		if ( --fhp->instances <= 0 )
		{
			freeFuseFHidx(&ourSuper,fi->fh);
			fi->fh = 0;
		}
		pthread_mutex_unlock(&ourMutex);
	}
	return 0;
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

const struct fuse_operations mgwfsf_oper =
{
	.init       = mgwfsf_init,
	.getattr	= mgwfsf_getattr,
	.readdir	= mgwfsf_readdir,
	.open		= mgwfsf_open,
	.read		= mgwfsf_read,
	.release	= mgwfsf_release,
	.statfs		= mgwfsf_statfs,
};


