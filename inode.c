#include "kmgwfs.h"

void mgwfs_destroy_inode(struct inode *inode)
{
	MgwfsInode_t *mgwfs_inode = MGWFS_INODE(inode);

	printk(KERN_INFO "Freeing private data of inode %p (%lu)\n",
		   mgwfs_inode, inode->i_ino);
	if ( mgwfs_inode->contentsPtr )
		kfree(mgwfs_inode->contentsPtr);
	if ( mgwfs_inode->fileName )
		kfree(mgwfs_inode->fileName);
	kmem_cache_free(mgwfs_inode_cache, mgwfs_inode);
}

void mgwfs_fill_inode(struct super_block *sb, struct inode *inode,
					  MgwfsInode_t *ourInode, const char *fName)
{
	struct timespec64 ts;
	MgwfsSuper_t *ourSuper = MGWFS_SB(sb);
	int fnLen;
	inode->i_mode = ourInode->mode;
	inode->i_size = ourInode->size;
	inode->i_sb = sb;
	inode->i_ino = ourInode->inode_no;
	inode->i_op = &mgwfs_inode_ops;
	inode_set_atime_to_ts(inode, current_time(inode));
	ts.tv_nsec = 0;
	ts.tv_sec = ourInode->mtime;
	inode_set_mtime_to_ts(inode, ts);
	ts.tv_sec = ourInode->ctime;
	inode_set_ctime_to_ts(inode, ts);
	if ( fName && (fnLen=strlen(fName)) )
	{
		++fnLen;
		if ( !(ourInode->fileName=(char *)kzalloc(fnLen,GFP_KERNEL)) )
			printk(KERN_ERR "mgwfs(): Out of memory kzalloc'ing %d bytes for filename", fnLen);
		else
			strncpy(ourInode->fileName,fName,fnLen);
	}
	inode->i_private = ourInode;

	if ( S_ISDIR(inode->i_mode) )
	{
		inode->i_fop = &mgwfs_dir_operations;
	}
	else if ( S_ISREG(inode->i_mode) )
	{
		inode->i_fop = &mgwfs_file_operations;
	}
	else
	{
		printk(KERN_WARNING
			   "Inode %lu is neither a directory nor a regular file",
			   inode->i_ino);
		inode->i_fop = NULL;
	}
	if ( (ourSuper->flags&MGWFS_MNT_OPT_VERBOSE_INODE) )
	{
		pr_info("mgwfs_fill_inode(): inode_no=%ld, file=%s, type=%s\n",
				inode->i_ino,
				ourInode->fileName ? ourInode->fileName:"<undefined>",
				S_ISDIR(inode->i_mode)?"DIR":"REG");
	}
}

#if 0
int mgwfs_alloc_mgwfs_inode(struct super_block *sb, uint64_t *out_inode_no)
{
	MgwfsSuper_t *ourSuper;
	struct buffer_head *bh;
	uint64_t i;
	int ret;
	char *bitmap;
	char *slot;
	char needle;

	ourSuper = MGWFS_SB(sb);

	mutex_lock(&mgwfs_sb_lock);

	bh = sb_bread(sb, MGWFS_INODE_BITMAP_BLOCK_NO);
	BUG_ON(!bh);

	bitmap = bh->b_data;
	ret = -ENOSPC;
	for ( i = 0; i < mgwfs_sb->inode_table_size; i++ )
	{
		slot = bitmap + i / BITS_IN_BYTE;
		needle = 1 << (i % BITS_IN_BYTE);
		if ( 0 == (*slot & needle) )
		{
			*out_inode_no = i;
			*slot |= needle;
			mgwfs_sb->inode_count += 1;
			ret = 0;
			break;
		}
	}

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);
	mgwfs_save_sb(sb);

	mutex_unlock(&mgwfs_sb_lock);
	return ret;
}
#endif

MgwfsInode_t* mgwfs_get_mgwfs_inode(struct super_block *sb, uint32_t inode_no, int generation, const char *fileName )
{
	MgwfsSuper_t *ourSuper = (MgwfsSuper_t *)sb->s_fs_info;
	MgwfsInode_t *ourInode;
	FsysHeader hdr;

	if ( !mgwfs_getFileHeader(sb, fileName, FSYS_ID_HEADER, ourSuper->indexSys + inode_no * FSYS_MAX_ALTS, &hdr) )
		return NULL;
	if ( generation && hdr.generation != generation )
		return NULL;
	ourInode = (MgwfsInode_t *)kmem_cache_alloc(mgwfs_inode_cache, GFP_KERNEL);
	memset(ourInode,0,sizeof(MgwfsInode_t));
	ourInode->clusters = hdr.clusters;
	ourInode->ctime = hdr.ctime;
	ourInode->inode_no = inode_no;
	ourInode->mode = (hdr.type == FSYS_TYPE_DIR) ? S_IFDIR|0555:S_IFREG|0444;
	ourInode->mtime = hdr.mtime;
	memcpy(ourInode->pointers,hdr.pointers,sizeof(ourInode->pointers));
	ourInode->size = hdr.size;
	return ourInode;
}

#if 0
void mgwfs_save_mgwfs_inode(struct super_block *sb,
							struct mgwfs_inode *inode_buf)
{
	struct buffer_head *bh;
	struct mgwfs_inode *inode;
	uint64_t inode_no;

	inode_no = inode_buf->inode_no;
	bh = sb_bread(sb, MGWFS_INODE_TABLE_START_BLOCK_NO + MGWFS_INODE_BLOCK_OFFSET(sb, inode_no));
	BUG_ON(!bh);

	inode = (struct mgwfs_inode *)(bh->b_data + MGWFS_INODE_BYTE_OFFSET(sb, inode_no));
	memcpy(inode, inode_buf, sizeof(*inode));

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);
}

int mgwfs_add_dir_record(struct super_block *sb, struct inode *dir,
						 struct dentry *dentry, struct inode *inode)
{
	struct buffer_head *bh;
	struct mgwfs_inode *parent_mgwfs_inode;
	struct mgwfs_dir_record *dir_record;

	parent_mgwfs_inode = MGWFS_INODE(dir);
	if ( unlikely(parent_mgwfs_inode->dir_children_count
				  >= MGWFS_DIR_MAX_RECORD(sb)) )
	{
		return -ENOSPC;
	}

	bh = sb_bread(sb, parent_mgwfs_inode->data_block_no);
	BUG_ON(!bh);

	dir_record = (struct mgwfs_dir_record *)bh->b_data;
	dir_record += parent_mgwfs_inode->dir_children_count;
	dir_record->inode_no = inode->i_ino;
	strcpy(dir_record->filename, dentry->d_name.name);

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);

	parent_mgwfs_inode->dir_children_count += 1;
	mgwfs_save_mgwfs_inode(sb, parent_mgwfs_inode);

	return 0;
}

int mgwfs_alloc_data_block(struct super_block *sb, uint64_t *out_data_block_no)
{
	struct mgwfs_superblock *mgwfs_sb;
	struct buffer_head *bh;
	uint64_t i;
	int ret;
	char *bitmap;
	char *slot;
	char needle;

	mgwfs_sb = MGWFS_SB(sb);

	mutex_lock(&mgwfs_sb_lock);

	bh = sb_bread(sb, MGWFS_DATA_BLOCK_BITMAP_BLOCK_NO);
	BUG_ON(!bh);

	bitmap = bh->b_data;
	ret = -ENOSPC;
	for ( i = 0; i < mgwfs_sb->data_block_table_size; i++ )
	{
		slot = bitmap + i / BITS_IN_BYTE;
		needle = 1 << (i % BITS_IN_BYTE);
		if ( 0 == (*slot & needle) )
		{
			*out_data_block_no
				= MGWFS_DATA_BLOCK_TABLE_START_BLOCK_NO(sb) + i;
			*slot |= needle;
			mgwfs_sb->data_block_count += 1;
			ret = 0;
			break;
		}
	}

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);
	mgwfs_save_sb(sb);

	mutex_unlock(&mgwfs_sb_lock);
	return ret;
}
#endif
#if 0
int mgwfs_create_inode(struct inode *dir, struct dentry *dentry,
					   umode_t mode)
{
	struct super_block *sb;
	struct mgwfs_superblock *mgwfs_sb;
	uint64_t inode_no;
	struct mgwfs_inode *mgwfs_inode;
	struct inode *inode;
	int ret;

	sb = dir->i_sb;
	mgwfs_sb = MGWFS_SB(sb);

	/* Create mgwfs_inode */
	ret = mgwfs_alloc_mgwfs_inode(sb, &inode_no);
	if ( 0 != ret )
	{
		printk(KERN_ERR "Unable to allocate on-disk inode. "
			   "Is inode table full? "
			   "Inode count: %llu\n",
			   mgwfs_sb->inode_count);
		return -ENOSPC;
	}
	mgwfs_inode = kmem_cache_alloc(mgwfs_inode_cache, GFP_KERNEL);
	mgwfs_inode->inode_no = inode_no;
	mgwfs_inode->mode = mode;
	if ( S_ISDIR(mode) )
	{
		mgwfs_inode->dir_children_count = 0;
	}
	else if ( S_ISREG(mode) )
	{
		mgwfs_inode->file_size = 0;
	}
	else
	{
		printk(KERN_WARNING
			   "Inode %llu is neither a directory nor a regular file",
			   inode_no);
	}

	/* Allocate data block for the new mgwfs_inode */
	ret = mgwfs_alloc_data_block(sb, &mgwfs_inode->data_block_no);
	if ( 0 != ret )
	{
		printk(KERN_ERR "Unable to allocate on-disk data block. "
			   "Is data block table full? "
			   "Data block count: %llu\n",
			   mgwfs_sb->data_block_count);
		return -ENOSPC;
	}

	/* Create VFS inode */
	inode = new_inode(sb);
	if ( !inode )
	{
		return -ENOMEM;
	}
	mgwfs_fill_inode(sb, inode, mgwfs_inode);

	/* Add new inode to parent dir */
	ret = mgwfs_add_dir_record(sb, dir, dentry, inode);
	if ( 0 != ret )
	{
		printk(KERN_ERR "Failed to add inode %lu to parent dir %lu\n",
			   inode->i_ino, dir->i_ino);
		return -ENOSPC;
	}

	inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
	d_add(dentry, inode);

	/* TODO we should free newly allocated inodes when error occurs */

	return 0;
}

int mgwfs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry,
				 umode_t mode, bool excl)
{
	return mgwfs_create_inode(dir, dentry, mode);
}

int mgwfs_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry,
				umode_t mode)
{
	/* @Sankar: The mkdir callback does not have S_IFDIR set.
	   Even ext2 sets it explicitly. Perhaps this is a bug */
	mode |= S_IFDIR;
	return mgwfs_create_inode(dir, dentry, mode);
}
#endif

struct dentry* mgwfs_lookup(struct inode *dir,
							struct dentry *child_dentry,
							unsigned int flags)
{
	MgwfsInode_t *parentInode = MGWFS_INODE(dir);
	struct super_block *sb = dir->i_sb;
	MgwfsSuper_t *ourSuper = (MgwfsSuper_t *)sb->s_fs_info;
	uint8_t *dirContents;
	int dirEnt;
	
	if ( !(dir->i_mode&S_IFDIR) )
	{
		printk(KERN_ERR "mgwfs_lookup(): file %s (fid=0x%X) is not a directory\n", parentInode->fileName ? parentInode->fileName : "<unknown>", parentInode->inode_no);
		return NULL;
	}
	if ( !(dirContents = parentInode->contentsPtr) )
	{
		dirContents = (uint8_t*)kzalloc(parentInode->size,GFP_KERNEL);
		if ( !dirContents )
		{
			printk(KERN_ERR "mgwfs_lookup(): Out of memory allocating %d bytes for directory file %s (fid=%d).\n",
				   parentInode->size, parentInode->fileName ? parentInode->fileName : "<unknown>", parentInode->inode_no);
			return NULL;
		}
		if ( !mgwfs_readFile(sb,child_dentry->d_name.name,dirContents,parentInode->size,parentInode->pointers[0]))
		{
			kfree(dirContents);
			printk(KERN_ERR "mgwfs_lookup(): Failed to read directory contents of %s.\n",
				   parentInode->fileName ? parentInode->fileName : "<unknown>");
			return NULL;
		}
		parentInode->contentsPtr = dirContents;
	}
	dirEnt = 0;
	while ( dirContents < dirContents+parentInode->size )
	{
		int txtLen;
		uint8_t gen;
		uint32_t fid;
		MgwfsInode_t *mgwfs_child_inode;
		struct inode *child_inode;
		int skipDots;
		
		fid = (dirContents[2]<<16)|(dirContents[1]<<8)|dirContents[0];
		if ( fid == 0 )
			break;
		dirContents += 3;
		gen = *dirContents++;
		txtLen = *dirContents++;
		if ( !txtLen )
			txtLen = 256;
		skipDots = 0;
		if ( dirContents[0] == '.' )
		{
			if ( txtLen == 1 )
				skipDots = 1;
			else if ( txtLen == 2 && dirContents[1] == '.' )
				skipDots = 1;
		}
		if ( !skipDots && !strcmp(dirContents, child_dentry->d_name.name) )
		{
			if ( (ourSuper->flags&MGWFS_MNT_OPT_VERBOSE_DIR) )
				printk( KERN_INFO "mgwfs_lookup():%5d: 0x%08X 0x%02X %3d %s\n", dirEnt, fid, gen, txtLen, dirContents);
			mgwfs_child_inode = mgwfs_get_mgwfs_inode(sb, fid, gen, (const char *)dirContents);
			if ( mgwfs_child_inode )
			{
				child_inode = new_inode(sb);
				if ( !child_inode )
				{
					printk(KERN_ERR "mgwfs_lookup(): Out of memory creating new inode for %s (fid=0x%X).\n", dirContents, fid);
					return NULL;
				}
				mgwfs_fill_inode(sb, child_inode, mgwfs_child_inode, dirContents);
				inode_init_owner(&nop_mnt_idmap, child_inode, dir, mgwfs_child_inode->mode);
				d_add(child_dentry, child_inode);
			}
			else
			{
				printk(KERN_ERR "mgwfs_lookup(): Failed to read file header for %s (fid=0x%X, gen=%d).\n", dirContents, fid, gen);
			}
			return NULL;
		}
		++dirEnt;
		dirContents += txtLen;
	}
	printk(KERN_ERR "mgwfs_lookup(): No inode found for the filename: %s\n", child_dentry->d_name.name);
	return NULL;
}
