#include "kmgwfs.h"

/* NOTE: This function may be called after "our" superblock has been free'd as part of umount and/or module removal (mgwfs_exit()) */
void mgwfs_destroy_inode(struct inode *inode)
{
	MgwfsInode_t *mgwfs_inode;

	mgwfs_inode = MGWFS_INODE(inode);
	if ( mgwfs_inode )
	{
		struct super_block *sb;
		MgwfsSuper_t *ourSuper=NULL;

		sb = inode->i_sb;
		if ( sb )
			ourSuper = MGWFS_SB(sb);
		if ( !ourSuper || (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_INODE) )
			pr_info("mgwfs_destroy_inode(): Freeing private data of inode %p (%lu). ourSuper=%p, contents=%p, fileName=%p, bh=%p\n",
					mgwfs_inode, inode->i_ino, ourSuper, mgwfs_inode->contentsPtr, mgwfs_inode->fileName, mgwfs_inode->buffer.bh);
		if ( mgwfs_inode->contentsPtr )
			kfree(mgwfs_inode->contentsPtr);
		if ( mgwfs_inode->fileName )
			kfree(mgwfs_inode->fileName);
		if ( mgwfs_inode->buffer.bh )
			brelse(mgwfs_inode->buffer.bh);
		kmem_cache_free(mgwfs_inode_cache, mgwfs_inode);
	}
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
	MgwfsInode_t *ourInode=NULL;
	FsysHeader hdr;

	if ( mgwfs_getFileHeader(sb, fileName, FSYS_ID_HEADER, inode_no, ourSuper->indexSys + inode_no * FSYS_MAX_ALTS, &hdr) )
	{
		if ( !generation || hdr.generation == generation )
		{
			ourInode = (MgwfsInode_t *)kmem_cache_alloc(mgwfs_inode_cache, GFP_KERNEL);
			memset(ourInode,0,sizeof(MgwfsInode_t));
			ourInode->clusters = hdr.clusters;
			ourInode->ctime = hdr.ctime;
			ourInode->inode_no = inode_no;
			ourInode->mode = (hdr.type == FSYS_TYPE_DIR) ? S_IFDIR|0555:S_IFREG|0444;
			ourInode->mtime = hdr.mtime;
			memcpy(ourInode->pointers,hdr.pointers,sizeof(ourInode->pointers));
			ourInode->size = hdr.size;
		}
		else
			pr_err("mgwfs_get_mgwfs_inode(): Failed generation match. Generation=%d, hdr.generation=%d\n",
				   generation, hdr.generation);
	}
	else
		pr_err("mgwfs_get_mgwfs_inode(): Failed mgwfs_getFileHeader(,'%s',,%d,...). Generation=%d\n",
			   fileName, inode_no, generation );
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
		if ( !mgwfs_readFile(sb,child_dentry->d_name.name,dirContents,parentInode->size,parentInode->pointers[0],0))
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
	if ( (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_DIR) )
		printk(KERN_WARNING "mgwfs_lookup(): No inode found for the filename: %s\n", child_dentry->d_name.name);
	return NULL;
}

#if 0
static int mgwfs_remove_from_dir(struct inode *dir, struct dentry *dentry)
{
    struct super_block *sb = dir->i_sb;
    struct inode *inode = d_inode(dentry);
    struct buffer_head *bh = NULL, *bh2 = NULL, *bh_prev = NULL;
    MgwfsSuper_t *eblock = NULL;
	MgwfsInode_t *ourDirInode;
    int ret = 0, found = false;
	uint8_t *dataPtr;
	
    /* Read parent directory index */
	ourDirInode = MGWFS_INODE(dir);
	dataPtr = (uint8_t *)ourDirInode->contentsPtr;
	while ( dataPtr < dataPtr+ourDirInode->size )
	{
        if (!eblock->extents[ei].ee_start)
            break;

        for (bi = 0; bi < eblock->extents[ei].ee_len; bi++) {
            bh2 = sb_bread(sb, eblock->extents[ei].ee_start + bi);
            if (!bh2) {
                ret = -EIO;
                goto release_bh;
            }
            dblock = (struct mgwfs_dir_block *) bh2->b_data;
            if (!dblock->files[0].inode)
                break;

            if (found) {
                memmove(dblock_prev->files + MGWFS_FILES_PER_BLOCK - 1,
                        dblock->files, sizeof(struct mgwfs_file));
                brelse(bh_prev);

                memmove(dblock->files, dblock->files + 1,
                        (MGWFS_FILES_PER_BLOCK - 1) *
                            sizeof(struct mgwfs_file));
                memset(dblock->files + MGWFS_FILES_PER_BLOCK - 1, 0,
                       sizeof(struct mgwfs_file));
                mark_buffer_dirty(bh2);

                bh_prev = bh2;
                dblock_prev = dblock;
                continue;
            }
            /* Remove file from parent directory */
            for (fi = 0; fi < MGWFS_FILES_PER_BLOCK; fi++) {
                if (dblock->files[fi].inode == inode->i_ino &&
                    !strcmp(dblock->files[fi].filename, dentry->d_name.name)) {
                    found = true;
                    if (fi != MGWFS_FILES_PER_BLOCK - 1) {
                        memmove(dblock->files + fi, dblock->files + fi + 1,
                                (MGWFS_FILES_PER_BLOCK - fi - 1) *
                                    sizeof(struct mgwfs_file));
                    }
                    memset(dblock->files + MGWFS_FILES_PER_BLOCK - 1, 0,
                           sizeof(struct mgwfs_file));
                    mark_buffer_dirty(bh2);
                    bh_prev = bh2;
                    dblock_prev = dblock;
                    break;
                }
            }
            if (!found)
                brelse(bh2);
        }
    }
    if (found) {
        if (bh_prev)
            brelse(bh_prev);
        eblock->nr_files--;
        mark_buffer_dirty(bh);
    }
release_bh:
    brelse(bh);
    return ret;
}

/* Remove a link for a file including the reference in the parent directory.
 * If link count is 0, destroy file in this way:
 *   - remove the file from its parent directory.
 *   - cleanup blocks containing data
 *   - cleanup file index block
 *   - cleanup inode
 */
static int mgwfs_unlink(struct inode *dir, struct dentry *dentry)
{
    struct super_block *sb = dir->i_sb;
    struct mgwfs_sb_info *sbi = MGWFS_SB(sb);
    struct inode *inode = d_inode(dentry);
    struct buffer_head *bh = NULL, *bh2 = NULL;
    struct mgwfs_file_ei_block *file_block = NULL;
#if MGWFS_AT_LEAST(6, 6, 0) && MGWFS_LESS_EQUAL(6, 7, 0)
    struct timespec64 cur_time;
#endif
    int ei = 0, bi = 0;
    int ret = 0;

    uint32_t ino = inode->i_ino;
    uint32_t bno = 0;

    ret = mgwfs_remove_from_dir(dir, dentry);
    if (ret != 0)
        return ret;

    if (S_ISLNK(inode->i_mode))
        goto clean_inode;

        /* Update inode stats */
#if MGWFS_AT_LEAST(6, 7, 0)
    simple_inode_init_ts(dir);
#elif MGWFS_AT_LEAST(6, 6, 0)
    cur_time = current_time(dir);
    dir->i_mtime = dir->i_atime = cur_time;
    inode_set_ctime_to_ts(dir, cur_time);
#else
    dir->i_mtime = dir->i_atime = dir->i_ctime = current_time(dir);
#endif

    if (S_ISDIR(inode->i_mode)) {
        drop_nlink(dir);
        drop_nlink(inode);
    }
    mark_inode_dirty(dir);

    if (inode->i_nlink > 1) {
        inode_dec_link_count(inode);
        return ret;
    }

    /* Cleans up pointed blocks when unlinking a file. If reading the index
     * block fails, the inode is cleaned up regardless, resulting in the
     * permanent loss of this file's blocks. If scrubbing a data block fails,
     * do not terminate the operation (as it is already too late); instead,
     * release the block and proceed.
     */
    bno = MGWFS_INODE(inode)->ei_block;
    bh = sb_bread(sb, bno);
    if (!bh)
        goto clean_inode;
    file_block = (struct mgwfs_file_ei_block *) bh->b_data;
    if (S_ISDIR(inode->i_mode))
        goto scrub;

    for (ei = 0; ei < MGWFS_MAX_EXTENTS; ei++) {
        char *block;

        if (!file_block->extents[ei].ee_start)
            break;

        put_blocks(sbi, file_block->extents[ei].ee_start,
                   file_block->extents[ei].ee_len);

        /* Scrub the extent */
        for (bi = 0; bi < file_block->extents[ei].ee_len; bi++) {
            bh2 = sb_bread(sb, file_block->extents[ei].ee_start + bi);
            if (!bh2)
                continue;
            block = (char *) bh2->b_data;
            memset(block, 0, MGWFS_BLOCK_SIZE);
            mark_buffer_dirty(bh2);
            brelse(bh2);
        }
    }

scrub:
    /* Scrub index block */
    memset(file_block, 0, MGWFS_BLOCK_SIZE);
    mark_buffer_dirty(bh);
    brelse(bh);

clean_inode:
    /* Cleanup inode and mark dirty */
    inode->i_blocks = 0;
    MGWFS_INODE(inode)->ei_block = 0;
    inode->i_size = 0;
    i_uid_write(inode, 0);
    i_gid_write(inode, 0);

#if MGWFS_AT_LEAST(6, 7, 0)
    inode_set_mtime(inode, 0, 0);
    inode_set_atime(inode, 0, 0);
    inode_set_ctime(inode, 0, 0);
#elif MGWFS_AT_LEAST(6, 6, 0)
    inode->i_mtime.tv_sec = inode->i_atime.tv_sec = 0;
    inode_set_ctime(inode, 0, 0);
#else
    inode->i_ctime.tv_sec = inode->i_mtime.tv_sec = inode->i_atime.tv_sec = 0;
#endif

    inode_dec_link_count(inode);

    /* Free inode and index block from bitmap */
    if (!S_ISLNK(inode->i_mode))
        put_blocks(sbi, bno, 1);
    inode->i_mode = 0;
    put_inode(sbi, ino);

    return ret;
}

#if MGWFS_AT_LEAST(6, 3, 0)
static int mgwfs_rename(struct mnt_idmap *id,
                           struct inode *old_dir,
                           struct dentry *old_dentry,
                           struct inode *new_dir,
                           struct dentry *new_dentry,
                           unsigned int flags)
#elif MGWFS_AT_LEAST(5, 12, 0)
static int mgwfs_rename(struct user_namespace *ns,
                           struct inode *old_dir,
                           struct dentry *old_dentry,
                           struct inode *new_dir,
                           struct dentry *new_dentry,
                           unsigned int flags)
#else
static int mgwfs_rename(struct inode *old_dir,
                           struct dentry *old_dentry,
                           struct inode *new_dir,
                           struct dentry *new_dentry,
                           unsigned int flags)
#endif
{
    struct super_block *sb = old_dir->i_sb;
    struct mgwfs_inode_info *ci_new = MGWFS_INODE(new_dir);
    struct inode *src = d_inode(old_dentry);
    struct buffer_head *bh_new = NULL, *bh2 = NULL;
    struct mgwfs_file_ei_block *eblock_new = NULL;
    struct mgwfs_dir_block *dblock = NULL;

#if MGWFS_AT_LEAST(6, 6, 0) && MGWFS_LESS_EQUAL(6, 7, 0)
    struct timespec64 cur_time;
#endif

    int new_pos = -1, ret = 0;
    int ei = 0, bi = 0, fi = 0, bno = 0;

    /* fail with these unsupported flags */
    if (flags & (RENAME_EXCHANGE | RENAME_WHITEOUT))
        return -EINVAL;

    /* Check if filename is not too long */
    if (strlen(new_dentry->d_name.name) > MGWFS_FILENAME_LEN)
        return -ENAMETOOLONG;

    /* Fail if new_dentry exists or if new_dir is full */
    bh_new = sb_bread(sb, ci_new->ei_block);
    if (!bh_new)
        return -EIO;

    eblock_new = (struct mgwfs_file_ei_block *) bh_new->b_data;
    for (ei = 0; new_pos < 0 && ei < MGWFS_MAX_EXTENTS; ei++) {
        if (!eblock_new->extents[ei].ee_start)
            break;

        for (bi = 0; new_pos < 0 && bi < eblock_new->extents[ei].ee_len; bi++) {
            bh2 = sb_bread(sb, eblock_new->extents[ei].ee_start + bi);
            if (!bh2) {
                ret = -EIO;
                goto release_new;
            }

            dblock = (struct mgwfs_dir_block *) bh2->b_data;
            for (fi = 0; fi < MGWFS_FILES_PER_BLOCK; fi++) {
                if (new_dir == old_dir) {
                    if (!strncmp(dblock->files[fi].filename,
                                 old_dentry->d_name.name,
                                 MGWFS_FILENAME_LEN)) {
                        strncpy(dblock->files[fi].filename,
                                new_dentry->d_name.name, MGWFS_FILENAME_LEN);
                        mark_buffer_dirty(bh2);
                        brelse(bh2);
                        goto release_new;
                    }
                }
                if (!strncmp(dblock->files[fi].filename,
                             new_dentry->d_name.name, MGWFS_FILENAME_LEN)) {
                    brelse(bh2);
                    ret = -EEXIST;
                    goto release_new;
                }
                if (new_pos < 0 && !dblock->files[fi].inode) {
                    new_pos = fi;
                    break;
                }
            }

            brelse(bh2);
        }
    }

    /* If new directory is full, fail */
    if (new_pos < 0 && eblock_new->nr_files == MGWFS_FILES_PER_EXT) {
        ret = -EMLINK;
        goto release_new;
    }

    /* insert in new parent directory */
    /* Get new freeblocks for extent if needed*/
    if (new_pos < 0) {
        bno = get_free_blocks(sb, 8);
        if (!bno) {
            ret = -ENOSPC;
            goto release_new;
        }
        eblock_new->extents[ei].ee_start = bno;
        eblock_new->extents[ei].ee_len = 8;
        eblock_new->extents[ei].ee_block =
            ei ? eblock_new->extents[ei - 1].ee_block +
                     eblock_new->extents[ei - 1].ee_len
               : 0;
        bh2 = sb_bread(sb, eblock_new->extents[ei].ee_start + 0);
        if (!bh2) {
            ret = -EIO;
            goto put_block;
        }
        dblock = (struct mgwfs_dir_block *) bh2->b_data;
        mark_buffer_dirty(bh_new);
        new_pos = 0;
    }
    dblock->files[new_pos].inode = src->i_ino;
    strncpy(dblock->files[new_pos].filename, new_dentry->d_name.name,
            MGWFS_FILENAME_LEN);
    mark_buffer_dirty(bh2);
    brelse(bh2);

    /* Update new parent inode metadata */
#if MGWFS_AT_LEAST(6, 7, 0)
    simple_inode_init_ts(new_dir);
#elif MGWFS_AT_LEAST(6, 6, 0)
    cur_time = current_time(new_dir);
    new_dir->i_atime = new_dir->i_mtime = cur_time;
    inode_set_ctime_to_ts(new_dir, cur_time);
#else
    new_dir->i_atime = new_dir->i_ctime = new_dir->i_mtime =
        current_time(new_dir);
#endif

    if (S_ISDIR(src->i_mode))
        inc_nlink(new_dir);
    mark_inode_dirty(new_dir);

    /* remove target from old parent directory */
    ret = mgwfs_remove_from_dir(old_dir, old_dentry);
    if (ret != 0)
        goto release_new;

        /* Update old parent inode metadata */
#if MGWFS_AT_LEAST(6, 7, 0)
    simple_inode_init_ts(old_dir);
#elif MGWFS_AT_LEAST(6, 6, 0)
    cur_time = current_time(old_dir);
    old_dir->i_atime = old_dir->i_mtime = cur_time;
    inode_set_ctime_to_ts(old_dir, cur_time);
#else
    old_dir->i_atime = old_dir->i_ctime = old_dir->i_mtime =
        current_time(old_dir);
#endif

    if (S_ISDIR(src->i_mode))
        drop_nlink(old_dir);
    mark_inode_dirty(old_dir);

    return ret;

put_block:
    if (eblock_new->extents[ei].ee_start) {
        put_blocks(MGWFS_SB(sb), eblock_new->extents[ei].ee_start,
                   eblock_new->extents[ei].ee_len);
        memset(&eblock_new->extents[ei], 0, sizeof(struct mgwfs_extent));
    }
release_new:
    brelse(bh_new);
    return ret;
}

#if MGWFS_AT_LEAST(6, 3, 0)
static int mgwfs_mkdir(struct mnt_idmap *id,
                          struct inode *dir,
                          struct dentry *dentry,
                          umode_t mode)
{
    return mgwfs_create(id, dir, dentry, mode | S_IFDIR, 0);
}
#elif MGWFS_AT_LEAST(5, 12, 0)
static int mgwfs_mkdir(struct user_namespace *ns,
                          struct inode *dir,
                          struct dentry *dentry,
                          umode_t mode)
{
    return mgwfs_create(ns, dir, dentry, mode | S_IFDIR, 0);
}
#else
static int mgwfs_mkdir(struct inode *dir,
                          struct dentry *dentry,
                          umode_t mode)
{
    return mgwfs_create(dir, dentry, mode | S_IFDIR, 0);
}
#endif

static int mgwfs_rmdir(struct inode *dir, struct dentry *dentry)
{
    struct super_block *sb = dir->i_sb;
    struct inode *inode = d_inode(dentry);
    struct buffer_head *bh;
    struct mgwfs_file_ei_block *eblock;

    /* If the directory is not empty, fail */
    if (inode->i_nlink > 2)
        return -ENOTEMPTY;

    bh = sb_bread(sb, MGWFS_INODE(inode)->ei_block);
    if (!bh)
        return -EIO;

    eblock = (struct mgwfs_file_ei_block *) bh->b_data;
    if (eblock->nr_files != 0) {
        brelse(bh);
        return -ENOTEMPTY;
    }
    brelse(bh);

    /* Remove directory with unlink */
    return mgwfs_unlink(dir, dentry);
}

static int mgwfs_link(struct dentry *old_dentry,
                         struct inode *dir,
                         struct dentry *dentry)
{
    struct inode *inode = d_inode(old_dentry);
    struct super_block *sb = inode->i_sb;
    struct mgwfs_inode_info *ci_dir = MGWFS_INODE(dir);
    struct mgwfs_file_ei_block *eblock = NULL;
    struct mgwfs_dir_block *dblock;
    struct buffer_head *bh = NULL, *bh2 = NULL;
    int ret = 0, alloc = false, bno = 0;
    int ei = 0, bi = 0, fi = 0;

    bh = sb_bread(sb, ci_dir->ei_block);
    if (!bh)
        return -EIO;

    eblock = (struct mgwfs_file_ei_block *) bh->b_data;
    if (eblock->nr_files == MGWFS_MAX_SUBFILES) {
        ret = -EMLINK;
        printk(KERN_INFO "directory is full");
        goto end;
    }

    ei = eblock->nr_files / MGWFS_FILES_PER_EXT;
    bi = eblock->nr_files % MGWFS_FILES_PER_EXT / MGWFS_FILES_PER_BLOCK;
    fi = eblock->nr_files % MGWFS_FILES_PER_BLOCK;

    if (eblock->extents[ei].ee_start == 0) {
        bno = get_free_blocks(sb, 8);
        if (!bno) {
            ret = -ENOSPC;
            goto end;
        }
        eblock->extents[ei].ee_start = bno;
        eblock->extents[ei].ee_len = 8;
        eblock->extents[ei].ee_block = ei ? eblock->extents[ei - 1].ee_block +
                                                eblock->extents[ei - 1].ee_len
                                          : 0;
        alloc = true;
    }
    bh2 = sb_bread(sb, eblock->extents[ei].ee_start + bi);
    if (!bh2) {
        ret = -EIO;
        goto put_block;
    }
    dblock = (struct mgwfs_dir_block *) bh2->b_data;

    dblock->files[fi].inode = inode->i_ino;
    strncpy(dblock->files[fi].filename, dentry->d_name.name,
            MGWFS_FILENAME_LEN);

    eblock->nr_files++;
    mark_buffer_dirty(bh2);
    mark_buffer_dirty(bh);
    brelse(bh2);
    brelse(bh);

    inode_inc_link_count(inode);
    ihold(inode);
    d_instantiate(dentry, inode);
    return ret;

put_block:
    if (alloc && eblock->extents[ei].ee_start) {
        put_blocks(MGWFS_SB(sb), eblock->extents[ei].ee_start,
                   eblock->extents[ei].ee_len);
        memset(&eblock->extents[ei], 0, sizeof(struct mgwfs_extent));
    }
end:
    brelse(bh);
    return ret;
}

#endif	/* read/write */
