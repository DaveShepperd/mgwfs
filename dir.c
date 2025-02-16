#include "kmgwfs.h"

int mgwfs_readdir(struct file *dirp, struct dir_context *ctx)
{
	int dirEnt;
	int pos;
	struct super_block *sb;
	struct inode *inode;
	MgwfsSuper_t *ourSuper;
	MgwfsInode_t *ourInode, *next;
	
	inode = file_inode(dirp);
	if ( !inode )
	{
		printk(KERN_ERR "mgwfs_readdir(): Could not get inode from dirp\n");
		return -EINVAL;
	}
	sb = inode->i_sb;
	ourSuper = (MgwfsSuper_t *)sb->s_fs_info;
	ourInode = MGWFS_INODE(inode);
	if ( !ourInode )
	{
		printk(KERN_ERR "mgwfs_readdir(): Could not get mgwfs_inode from file_inode(dirp)\n");
		return -EINVAL;
	}
	if ( !ctx )
	{
		printk(KERN_ERR "mgwfs_readdir(): ctx is NULL. inode=%p\n", inode);
		return -EINVAL;
	}
	pos = ctx->pos;
	if ( !ctx->actor )
	{
		printk(KERN_ERR "mgwfs_readdir(): ctx->actor is NULL.\n");
		return -EINVAL;
	}
	if ( (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_DIR) )
	{
		printk(KERN_INFO "mgwfs_readdir(): dirp=%p, ctx=%p, sb=%p, ourSuper=%p, ourInode=%p, ourInode->inode_no=0x%X, pos=%d, dir='%s'\n",
			   dirp, ctx, sb, ourSuper, ourInode, ourInode->inode_no, pos, file_dentry(dirp)->d_name.name);
	}
	if ( unlikely(!S_ISDIR(ourInode->mode)) )
	{
		printk(KERN_ERR "mgwfs_readdir(): Inode 0x%X of dentry %s is not a directory\n",
			   ourInode->inode_no,
			   file_dentry(dirp)->d_name.name); 
		return -ENOTDIR;
	}
	mutex_lock(&mgwfs_mutexLock);
	if ( !(next = ourInode->children) )
	{
		int err;
		if ( (err = mgwfs_unpackDir("mgwfs_readdir():", ourInode->parentDentry, ourSuper, inode)) )
		{
			mutex_unlock(&mgwfs_mutexLock);
			return err;
		}
		if ( !dir_emit_dots(dirp, ctx) )
		{
			mutex_unlock(&mgwfs_mutexLock);
			return 0;
		}
		next = ourInode->children;
		dirEnt = 0;
		for (; next; next = next->nextInode, ++dirEnt  )
		{
			if ( (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_DIR) )
				printk(KERN_INFO "mgwfs_readdir():%5d: 0x%08X 0x%02X(%3d) %s\n", dirEnt, next->inode_no, next->fnLen, next->fnLen, next->fileName);
			if ( !dir_emit(ctx, next->fileName, next->fnLen, next->inode_no, S_ISDIR(next->mode) ? DT_DIR : DT_REG) )
				break;
		}
		ctx->pos = ourInode->fsHeader.size;
	}

	mutex_unlock(&mgwfs_mutexLock);
	return 0;
}
