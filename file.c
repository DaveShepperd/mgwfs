#include "kmgwfs.h"

ssize_t mgwfs_read(struct file *filp, char __user *buf, size_t len,
				   loff_t *ppos)
{
	struct super_block *sb;
	struct inode *inode;
	MgwfsInode_t *ourInode;
	MgwfsSuper_t *ourSuper=NULL;
	FsysRetPtr *retPtr;
	int retIdx;         /* index into retrieval pointer set */
	int bytesRead;      /* total bytes read so far */
	int relativeSector; /* sector relative to current retrieval pointer set */
	sector_t actualSector; /* sector relative to media (i.e. 0=partition table) */
	int sectorOffset;   /* byte offset into sector read */
	const char *ourFileName = "<unknown>";
	int verbose=0;
	
	inode = filp->f_path.dentry->d_inode;
	sb = inode->i_sb;
	ourInode = MGWFS_INODE(inode);
	ourSuper = MGWFS_SB(sb); //ourInode->ourSuper;
	if ( ourInode->fileName )
		ourFileName = ourInode->fileName;
	if ( ourSuper && (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_READ) )
		verbose = 1;
	if ( !ourSuper || verbose )
	{
		pr_info("mgwfs_read(): file '%s': sb=%p, ourInode=%p, ourSuper=%p, *ppos=%lld, len=%ld, size=%d\n",
				ourFileName, sb, ourInode, ourSuper, *ppos, len, ourInode->size);
	}
	if ( *ppos >= ourInode->size || !len )
	{
		return 0;
	}
	relativeSector = *ppos / BYTES_PER_SECTOR;      /* starting sector */
	sectorOffset = *ppos % BYTES_PER_SECTOR;		/* offset into first sector */
	retIdx = 0;
	retPtr = ourInode->pointers[0];         /* pointer to list of retrieval pointers */
	if ( verbose )
	{
		pr_info("mgwfs_read(): Reading '%s'. pos=0x%llX. Looking for retrieval pointer set. relativeSector=0x%08X, sectorOffset=0x%X\n",
				ourFileName, *ppos, relativeSector, sectorOffset);
	}
	while ( retPtr->nblocks && retIdx < FSYS_MAX_FHPTRS && retPtr->nblocks < relativeSector )
	{
		relativeSector -= retPtr->nblocks;
		++retPtr;
		++retIdx;
	}
	if ( verbose )
	{
		if ( retIdx < FSYS_MAX_FHPTRS )
		{
			pr_info("mgwfs_read(): File '%s'. Retrieval pointer set %d. Starting sector 0x%08X, numSectors %d\n",
					ourFileName, retIdx, retPtr->start, retPtr->nblocks);
		}
		else
		{
			pr_info("mgwfs_read(): File '%s'. Didn't find a retrieval pointer set in %ld entries.\n",
					ourFileName, FSYS_MAX_FHPTRS);
		}
	}
	if ( retIdx >= FSYS_MAX_FHPTRS )
		return 0;
	bytesRead = 0;
	while ( bytesRead < len && retIdx < FSYS_MAX_FHPTRS )
	{
		char *bufPointer;
		int bytesToCopy;
		int numBytes;

		if ( relativeSector >= retPtr->nblocks )
		{
			relativeSector = 0;
			++retPtr;
			++retIdx;
			if ( verbose )
			{
				pr_info("mgwfs_read(): File '%s'. Ran off end of retrieval pointer set %d. Advanced to %d. Starting sector 0x%08X, numSectors %d\n",
						ourFileName, retIdx - 1, retIdx, retPtr->start, retPtr->nblocks);
			}
			continue;
		}
		actualSector = retPtr->start + relativeSector;
		bufPointer = (char *)mgwfs_getSector(sb, &ourInode->buffer, actualSector, &numBytes);
		if ( !bufPointer )
		{
			printk(KERN_ERR "mgwfs_read(): File '%s': Failed to read sector 0x%08llX\n",
				   ourFileName, actualSector);
			return 0;
		}
		bufPointer += sectorOffset;
		bytesToCopy = len - bytesRead;         /* Assume to read the max */
		if ( bytesToCopy > numBytes )       /* but clip to amount read by mgwfs_getSector() */
			bytesToCopy = numBytes;
		if ( relativeSector + bytesToCopy / BYTES_PER_SECTOR > retPtr->nblocks )
		{
			/* We're to read too much off this retrieval pointer, clip the count */
			bytesToCopy = (retPtr->nblocks - relativeSector) * BYTES_PER_SECTOR;
		}
		if ( bytesToCopy + *ppos > ourInode->size ) /* then clip again if to read beyond EOF */
			bytesToCopy = ourInode->size - *ppos;
		if ( bytesToCopy <= 0 )
		{
			pr_err("mgwfs_read(): file '%s' computed bytesToCopy as 0. len=%ld, bytesRead=%d, numBytes=%d, relativeSector=%d, nblocks=%d, *ppos=%lld, size=%d\n",
				   ourFileName, len, bytesRead, numBytes, relativeSector, retPtr->nblocks, *ppos, ourInode->size);
			break;
		}
		if ( verbose )
		{
			pr_info("mgwfs_read(): File '%s'. Read sector 0x%08llX. numBytes=%d. About to copy %d bytes to user buffer.\n",
					ourFileName, actualSector, numBytes, bytesToCopy);
		}
		if ( copy_to_user(buf, bufPointer, bytesToCopy) )
		{
			printk(KERN_ERR "mgwfs_read(): File '%s': Error copying file content to userspace buffer\n", ourFileName);
			return -EFAULT;
		}
		bytesRead += bytesToCopy;
		buf += bytesToCopy;
		*ppos += bytesToCopy;
		if ( *ppos >= ourInode->size )
			break;
		relativeSector += bytesToCopy / BYTES_PER_SECTOR;
		sectorOffset = bytesToCopy % BYTES_PER_SECTOR;
	}
	return bytesRead;
}

loff_t mgwfs_llseek(struct file *filp, loff_t offset, int whence)
{
	struct inode *inode;
	struct super_block *sb;
	MgwfsInode_t *ourInode;
	const char *ourFileName = "<unknown>";
	loff_t temp;
	int verbose=0;
	MgwfsSuper_t *ourSuper=NULL;
	
	inode = filp->f_path.dentry->d_inode;
	ourInode = MGWFS_INODE(inode);
	sb = filp->f_path.dentry->d_sb;
	if ( sb )
	{
		ourSuper = MGWFS_SB(sb);
		if ( !ourSuper || (ourSuper && (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_READ)) )
			verbose = 1;
	}
	if ( ourInode->fileName )
		ourFileName = ourInode->fileName;
	if ( verbose )
	{
		pr_info("mgwfs_llseek(): File '%s'. (ourSuper=%p) Before llseek: offset=%lld, whence=%d. Current position= %lld\n",
				ourFileName, ourSuper, offset, whence, filp->f_pos);
	}
	switch (whence)
	{
	case SEEK_SET:
		if ( (offset > ourInode->size) || (offset < 0) )
		{
			if ( verbose )
			{
				pr_info("mgwfs_llseek(): File '%s'. Seek SET out of range: offset=%lld is >= %d or < 0\n",
						ourFileName, offset, ourInode->size);
			}
			return -EINVAL;
		}
		filp->f_pos = offset;
		break;
	case SEEK_CUR:
		temp = filp->f_pos+offset;
		if ( (temp > ourInode->size) || (temp < 0) )
		{
			if ( verbose )
			{
				pr_info("mgwfs_llseek(): File '%s'. Seek CUR out of range: %lld%+lld(%lld) >= %d or < 0\n",
						ourFileName, filp->f_pos, offset, temp, ourInode->size);
			}
			return -EINVAL;
		}
		filp->f_pos = temp;
		break;
	case SEEK_END:
		temp = ourInode->size + offset;
		if ( (temp > ourInode->size) || (temp < 0) )
		{
			if ( verbose )
			{
				pr_info("mgwfs_llseek(): File '%s'. Seek END out of range: %d%+lld(%lld) >= %d or < 0\n",
						ourFileName, ourInode->size, offset, temp, ourInode->size);
			}
			return -EINVAL;
		}
		filp->f_pos = temp;
		break;
	default:
		if ( verbose )
		{
			pr_info("mgwfs_llseek(): File '%s'. Invalid 'whence' flag of %d. Can only be 0, 1 or 2.\n",
					ourFileName, whence);
		}
		return -EINVAL;
	}
	if ( verbose )
	{
		pr_info("mgwfs_llseek(): File '%s': After llseek: position: %lld\n",
				ourFileName, filp->f_pos);
	}
	return filp->f_pos;
}

#if 0
/* TODO We didn't use address_space/pagecache here.
   If we hook file_operations.write = do_sync_write,
   and file_operations.aio_write = generic_file_aio_write,
   we will use write to pagecache instead. */
ssize_t mgwfs_write(struct file *filp, const char __user *buf, size_t len,
					loff_t *ppos)
{
	struct super_block *sb;
	struct inode *inode;
	struct mgwfs_inode *mgwfs_inode;
	struct buffer_head *bh;
	struct mgwfs_superblock *mgwfs_sb;
	struct kiocb kiocb;
	struct iov_iter iiter;
	char *buffer;
	int ret;
	ssize_t retSize;

	init_sync_kiocb(&kiocb, filp);
	kiocb.ki_pos = *ppos;
	inode = file_dentry(filp)->d_inode;
	sb = inode->i_sb;
	mgwfs_inode = MGWFS_INODE(inode);
	mgwfs_sb = MGWFS_SB(sb);
	retSize = generic_write_checks(&kiocb, ppos, &len, 0);
	if (ret)
	{
		return ret;
	}

	bh = sb_bread(sb, mgwfs_inode->data_block_no);
	if (!bh)
	{
		printk(KERN_ERR "Failed to read data block %llu\n",
			   mgwfs_inode->data_block_no);
		return 0;
	}

	buffer = (char *)bh->b_data + *ppos;
	if (copy_from_user(buffer, buf, len))
	{
		brelse(bh);
		printk(KERN_ERR
			   "Error copying file content from userspace buffer "
			   "to kernel space\n");
		return -EFAULT;
	}
	*ppos += len;

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);

	mgwfs_inode->file_size = max((size_t)(mgwfs_inode->file_size),
								 (size_t)(*ppos));
	mgwfs_save_mgwfs_inode(sb, mgwfs_inode);

	/* TODO We didn't update file size here. To be frank I don't know how. */

	return len;
}
#endif

