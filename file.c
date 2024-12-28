#include "kmgwfs.h"

ssize_t mgwfs_read(struct file *filp, char __user *buf, size_t len,
				   loff_t *ppos)
{
	struct super_block *sb;
	struct inode *inode;
	MgwfsInode_t *ourInode;
	FsysRetPtr *retPtr;
	int retIdx;
	int bytesRead;
	int relativeSector;
	sector_t actualSector;
	int sectorOffset;	/* offset into first sector */
	
	inode = filp->f_path.dentry->d_inode;
	sb = inode->i_sb;
	ourInode = MGWFS_INODE(inode);

	if ( *ppos >= ourInode->size )
	{
		return 0;
	}
	relativeSector = *ppos/BYTES_PER_SECTOR;		/* starting sector */
	sectorOffset = *ppos%BYTES_PER_SECTOR;	/* offset into first sector */
	retIdx = 0;
	retPtr = ourInode->pointers[0];			/* pointer to list of retrieval pointers */
	while ( retPtr->nblocks && retIdx < FSYS_MAX_FHPTRS && retPtr->nblocks < relativeSector )
	{
		relativeSector -= retPtr->nblocks;
		++retPtr;
		++retIdx;
	}
	if ( retIdx >= FSYS_MAX_FHPTRS )
		return 0;
	bytesRead = 0;
	while ( bytesRead < len && retIdx < FSYS_MAX_FHPTRS )
	{
		char *bufPointer;
		int numBytes, bytesToCopy;
		
		if ( relativeSector >= retPtr->nblocks )
		{
			relativeSector = 0;
			++retPtr;
			++retIdx;
			continue;
		}
		actualSector = retPtr->start + relativeSector;
		bufPointer = (char *)mgwfs_getSector(sb,actualSector,&numBytes);
		if ( !bufPointer )
		{
			printk(KERN_ERR "Failed to read data block %llu\n",
				   actualSector);
			return 0;
		}
		bufPointer += sectorOffset;
		sectorOffset = 0;
		bytesToCopy= len-bytesRead;			/* Assume to read the max */
		if ( bytesToCopy > numBytes )		/* but clip to amount read by mgwfs_getSector() */
			bytesToCopy = numBytes;
		if ( relativeSector+bytesToCopy/BYTES_PER_SECTOR > retPtr->nblocks )
		{
			/* We're to read too much off this retrieval pointer, clip the count */
			bytesToCopy = (retPtr->nblocks-relativeSector)*BYTES_PER_SECTOR;
		}
		if ( bytesToCopy + *ppos > ourInode->size ) /* then clip again if to read beyond EOF */
			bytesToCopy = ourInode->size-*ppos;
		if ( copy_to_user(buf, bufPointer, bytesToCopy) )
		{
			printk(KERN_ERR
				   "Error copying file content to userspace buffer\n");
			return -EFAULT;
		}
		*ppos += bytesToCopy;
		bytesRead += bytesToCopy;
		relativeSector += bytesToCopy/BYTES_PER_SECTOR;
	}
	return bytesRead;
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

