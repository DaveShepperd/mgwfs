#include "kmgwfs.h"

void mgwfs_destroy_inode(struct inode *inode) {
    struct mgwfs_inode *mgwfs_inode = MGWFS_INODE(inode);

    printk(KERN_INFO "Freeing private data of inode %p (%lu)\n",
           mgwfs_inode, inode->i_ino);
    kmem_cache_free(mgwfs_inode_cache, mgwfs_inode);
}

void mgwfs_fill_inode(struct super_block *sb, struct inode *inode,
                        struct mgwfs_inode *mgwfs_inode) {
    inode->i_mode = mgwfs_inode->mode;
    inode->i_sb = sb;
    inode->i_ino = mgwfs_inode->inode_no;
    inode->i_op = &mgwfs_inode_ops;
	inode_set_atime_to_ts(inode, current_time(inode));
	inode_set_mtime_to_ts(inode, inode_get_atime(inode));
	inode_set_ctime_to_ts(inode, inode_get_atime(inode));
#if 0
    // TODO hope we can use mgwfs_inode to store timespec
    inode->i_atime = inode->i_mtime 
                   = inode->i_ctime
                   = CURRENT_TIME;
#endif
    inode->i_private = mgwfs_inode;    
    
    if (S_ISDIR(mgwfs_inode->mode)) {
        inode->i_fop = &mgwfs_dir_operations;
    } else if (S_ISREG(mgwfs_inode->mode)) {
        inode->i_fop = &mgwfs_file_operations;
    } else {
        printk(KERN_WARNING
               "Inode %lu is neither a directory nor a regular file",
               inode->i_ino);
        inode->i_fop = NULL;
    }

    /* TODO mgwfs_inode->file_size seems not reflected in inode */
}

/* TODO I didn't implement any function to dealloc mgwfs_inode */
int mgwfs_alloc_mgwfs_inode(struct super_block *sb, uint64_t *out_inode_no) {
    struct mgwfs_superblock *mgwfs_sb;
    struct buffer_head *bh;
    uint64_t i;
    int ret;
    char *bitmap;
    char *slot;
    char needle;

    mgwfs_sb = MGWFS_SB(sb);

    mutex_lock(&mgwfs_sb_lock);

    bh = sb_bread(sb, MGWFS_INODE_BITMAP_BLOCK_NO);
    BUG_ON(!bh);

    bitmap = bh->b_data;
    ret = -ENOSPC;
    for (i = 0; i < mgwfs_sb->inode_table_size; i++) {
        slot = bitmap + i / BITS_IN_BYTE;
        needle = 1 << (i % BITS_IN_BYTE);
        if (0 == (*slot & needle)) {
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

struct mgwfs_inode *mgwfs_get_mgwfs_inode(struct super_block *sb,
                                                uint64_t inode_no) {
    struct buffer_head *bh;
    struct mgwfs_inode *inode;
    struct mgwfs_inode *inode_buf;

    bh = sb_bread(sb, MGWFS_INODE_TABLE_START_BLOCK_NO + MGWFS_INODE_BLOCK_OFFSET(sb, inode_no));
    BUG_ON(!bh);
    
    inode = (struct mgwfs_inode *)(bh->b_data + MGWFS_INODE_BYTE_OFFSET(sb, inode_no));
    inode_buf = kmem_cache_alloc(mgwfs_inode_cache, GFP_KERNEL);
    memcpy(inode_buf, inode, sizeof(*inode_buf));

    brelse(bh);
    return inode_buf;
}

void mgwfs_save_mgwfs_inode(struct super_block *sb,
                                struct mgwfs_inode *inode_buf) {
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
                           struct dentry *dentry, struct inode *inode) {
    struct buffer_head *bh;
    struct mgwfs_inode *parent_mgwfs_inode;
    struct mgwfs_dir_record *dir_record;

    parent_mgwfs_inode = MGWFS_INODE(dir);
    if (unlikely(parent_mgwfs_inode->dir_children_count
            >= MGWFS_DIR_MAX_RECORD(sb))) {
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

int mgwfs_alloc_data_block(struct super_block *sb, uint64_t *out_data_block_no) {
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
    for (i = 0; i < mgwfs_sb->data_block_table_size; i++) {
        slot = bitmap + i / BITS_IN_BYTE;
        needle = 1 << (i % BITS_IN_BYTE);
        if (0 == (*slot & needle)) {
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

int mgwfs_create_inode(struct inode *dir, struct dentry *dentry,
                         umode_t mode) {
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
    if (0 != ret) {
        printk(KERN_ERR "Unable to allocate on-disk inode. "
                        "Is inode table full? "
                        "Inode count: %llu\n",
                        mgwfs_sb->inode_count);
        return -ENOSPC;
    }
    mgwfs_inode = kmem_cache_alloc(mgwfs_inode_cache, GFP_KERNEL);
    mgwfs_inode->inode_no = inode_no;
    mgwfs_inode->mode = mode;
    if (S_ISDIR(mode)) {
        mgwfs_inode->dir_children_count = 0;
    } else if (S_ISREG(mode)) {
        mgwfs_inode->file_size = 0;
    } else {
        printk(KERN_WARNING
               "Inode %llu is neither a directory nor a regular file",
               inode_no);
    }

    /* Allocate data block for the new mgwfs_inode */
    ret = mgwfs_alloc_data_block(sb, &mgwfs_inode->data_block_no);
    if (0 != ret) {
        printk(KERN_ERR "Unable to allocate on-disk data block. "
                        "Is data block table full? "
                        "Data block count: %llu\n",
                        mgwfs_sb->data_block_count);
        return -ENOSPC;
    }

    /* Create VFS inode */
    inode = new_inode(sb);
    if (!inode) {
        return -ENOMEM;
    }
    mgwfs_fill_inode(sb, inode, mgwfs_inode);

    /* Add new inode to parent dir */
    ret = mgwfs_add_dir_record(sb, dir, dentry, inode);
    if (0 != ret) {
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
                   umode_t mode, bool excl) {
    return mgwfs_create_inode(dir, dentry, mode);
}

int mgwfs_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry,
                  umode_t mode) {
    /* @Sankar: The mkdir callback does not have S_IFDIR set.
       Even ext2 sets it explicitly. Perhaps this is a bug */
    mode |= S_IFDIR;
    return mgwfs_create_inode(dir, dentry, mode);
}

struct dentry *mgwfs_lookup(struct inode *dir,
                              struct dentry *child_dentry,
                              unsigned int flags) {
    struct mgwfs_inode *parent_mgwfs_inode = MGWFS_INODE(dir);
    struct super_block *sb = dir->i_sb;
    struct buffer_head *bh;
    struct mgwfs_dir_record *dir_record;
    struct mgwfs_inode *mgwfs_child_inode;
    struct inode *child_inode;
    uint64_t i;

    bh = sb_bread(sb, parent_mgwfs_inode->data_block_no);
    BUG_ON(!bh);

    dir_record = (struct mgwfs_dir_record *)bh->b_data;
	pr_info("mgwfs_lookup(): parent=%p, sb=%p, dir=%p, child_dentry=%p, flags=0x%X, children=%lld\n",
			parent_mgwfs_inode, sb, dir, child_dentry, flags, parent_mgwfs_inode->dir_children_count);

    for (i = 0; i < parent_mgwfs_inode->dir_children_count; i++) {
        printk(KERN_INFO "mgwfs_lookup: i=%llu, dir_record->filename=%s, child_dentry->d_name.name=%s", i, dir_record->filename, child_dentry->d_name.name);    // TODO
        if (0 == strcmp(dir_record->filename, child_dentry->d_name.name)) {
            mgwfs_child_inode = mgwfs_get_mgwfs_inode(sb, dir_record->inode_no);
            child_inode = new_inode(sb);
            if (!child_inode) {
                printk(KERN_ERR "Cannot create new inode. No memory.\n");
                return NULL; 
            }
            mgwfs_fill_inode(sb, child_inode, mgwfs_child_inode);
            inode_init_owner(&nop_mnt_idmap, child_inode, dir, mgwfs_child_inode->mode);
            d_add(child_dentry, child_inode);
            return NULL;    
        }
        dir_record++;
    }

    printk(KERN_ERR
           "No inode found for the filename: %s\n",
           child_dentry->d_name.name);
    return NULL;
}
