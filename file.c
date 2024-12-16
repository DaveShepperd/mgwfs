#include "kmgwfs.h"

ssize_t mgwfs_read(struct file *filp, char __user *buf, size_t len,
                     loff_t *ppos) {
    struct super_block *sb;
    struct inode *inode;
    struct mgwfs_inode *mgwfs_inode;
    struct buffer_head *bh;
    char *buffer;
    int nbytes;

    inode = filp->f_path.dentry->d_inode;
    sb = inode->i_sb;
    mgwfs_inode = MGWFS_INODE(inode);
    
    if (*ppos >= mgwfs_inode->file_size) {
        return 0;
    }

    bh = sb_bread(sb, mgwfs_inode->data_block_no);
    if (!bh) {
        printk(KERN_ERR "Failed to read data block %llu\n",
               mgwfs_inode->data_block_no);
        return 0;
    }

    buffer = (char *)bh->b_data + *ppos;
    nbytes = min((size_t)(mgwfs_inode->file_size - *ppos), len);

    if (copy_to_user(buf, buffer, nbytes)) {
        brelse(bh);
        printk(KERN_ERR
               "Error copying file content to userspace buffer\n");
        return -EFAULT;
    }

    brelse(bh);
    *ppos += nbytes;
    return nbytes;
}

/* TODO We didn't use address_space/pagecache here.
   If we hook file_operations.write = do_sync_write,
   and file_operations.aio_write = generic_file_aio_write,
   we will use write to pagecache instead. */
ssize_t mgwfs_write(struct file *filp, const char __user *buf, size_t len,
                      loff_t *ppos) {
#if 0
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
    if (ret) {
        return ret;
    }

    bh = sb_bread(sb, mgwfs_inode->data_block_no);
    if (!bh) {
        printk(KERN_ERR "Failed to read data block %llu\n",
               mgwfs_inode->data_block_no);
        return 0;
    }

    buffer = (char *)bh->b_data + *ppos;
    if (copy_from_user(buffer, buf, len)) {
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
#else
	printk(KERN_ERR "mgwfs() No support for writing.\n");
	return -EFAULT;
#endif
}
