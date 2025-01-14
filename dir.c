#include "kmgwfs.h"

int mgwfs_readdir(struct file *dirp, struct dir_context *ctx)
{
	int dirEnt;
	int pos;
	struct super_block *sb;
	struct inode *inode;
	MgwfsSuper_t *ourSuper;
	MgwfsInode_t *ourInode;
	uint8_t *dirContents;
	
	
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
		printk(KERN_ERR "mgwfs_readdir(): ctx->actor is NULL. pos=%d, ctx->pos=%lld\n", pos, ctx->pos);
		return -EINVAL;
	}
	if ( (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_DIR) )
	{
		printk(KERN_INFO "mgwfs_readdir(): dirp=%p, ctx=%p, sb=%p, ourSuper=%p, ourInode=%p, ourInode->inode_no=0x%X, pos=%d, dir=%s\n",
			   dirp, ctx, sb, ourSuper, ourInode, ourInode->inode_no, pos, file_dentry(dirp)->d_name.name);
	}
	if ( pos )
	{
		// TODO @Sankar: we use a hack of reading pos to figure if we have filled in data.
		return 0;
	}

	if ( unlikely(!S_ISDIR(ourInode->mode)) )
	{
		printk(KERN_ERR "Inode 0x%X of dentry %s is not a directory\n",
			   ourInode->inode_no,
			   file_dentry(dirp)->d_name.name); 
		return -ENOTDIR;
	}

	if ( !dir_emit_dots(dirp, ctx) )
		return 0;

	if ( !(dirContents = ourInode->contentsPtr) )
	{
		dirContents = (uint8_t*)kzalloc(ourInode->size,GFP_KERNEL);
		if ( !dirContents )
		{
			printk(KERN_ERR "mgwfs_readdir(): Out of memory allocating %d bytes for directory file %s (fid=%d).\n",
				   ourInode->size, ourInode->fileName ? ourInode->fileName : "<unknown>", ourInode->inode_no);
			return 0;
		}
		if ( !mgwfs_readFile(sb,ourInode->fileName,dirContents,ourInode->size,ourInode->pointers[0],0))
		{
			kfree(dirContents);
			printk(KERN_ERR "mgwfs_readdir(): Failed to read directory contents of %s (fid=%d).\n",
				   ourInode->fileName ? ourInode->fileName : "<unknown>", ourInode->inode_no);
			return 0;
		}
		ourInode->contentsPtr = dirContents;
	}
	dirEnt = 0;
	while ( dirContents < dirContents+ourInode->size )
	{
		int txtLen, skipDots;
		uint8_t gen;
		uint32_t fid;

		fid = (dirContents[2]<<16)|(dirContents[1]<<8)|dirContents[0];
		if ( fid == 0 )
			break;
		dirContents += 3;
		gen = *dirContents++;
		txtLen = *dirContents++;
		if ( !txtLen )
			txtLen = 256;
		if ( dirContents[txtLen-1] )
		{
			dirContents[txtLen-1] = 0;
			printk(KERN_ERR "mgwfs_readdir(): Corrupted directory entry %d in %s:. fid=%d, fn=%s\n",
				   dirEnt, ourInode->fileName ? ourInode->fileName:"<unknown", fid, dirContents);
			break;
		}
		if ( (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_DIR) )
			printk( KERN_INFO "mgwfs_readdir():%5d: 0x%08X 0x%02X %3d %s\n", dirEnt, fid, gen, txtLen, dirContents);
		skipDots = 0;
		if ( dirContents[0] == '.' )
		{
			if ( txtLen == 1 )
				skipDots = 1;
			else if ( txtLen == 2 && dirContents[1] == '.' )
				skipDots = 1;
		}
		if ( !skipDots )
		{
			FsysHeader hdr;
			if ( mgwfs_getFileHeader(sb, dirContents, FSYS_ID_HEADER, fid, ourSuper->indexSys + fid*FSYS_MAX_ALTS, &hdr) )
			{
				if ( !dir_emit(ctx, dirContents, txtLen - 1, fid, hdr.type == FSYS_TYPE_DIR ? DT_DIR : DT_REG) )
					break;
			}
		}
		++dirEnt;
		dirContents += txtLen;
		ctx->pos += txtLen+5;
	}
	return 0;
}
