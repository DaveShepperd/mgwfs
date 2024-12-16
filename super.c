#include "kmgwfs.h"

#define MGWFS_MNT_OPT_RO 1
#define MGWFS_MNT_OPT_VERBOSE 2

static const match_table_t tokens = {
    {MGWFS_MNT_OPT_RO, "ro"},
	{MGWFS_MNT_OPT_VERBOSE, "verbose"},
	{0,NULL}
};

static int mgwfs_parse_options(struct super_block *sb, char *options)
{
    substring_t args[MAX_OPT_ARGS];
    int token;
    char *p;

    pr_info("mgwfs_parse_options: parsing options '%s'\n", options);

    while ((p = strsep(&options, ","))) {
        if (!*p)
            continue;

        args[0].to = args[0].from = NULL;
        token = match_token(p, tokens, args);
		if ( token == MGWFS_MNT_OPT_RO )
		{
			pr_info("mgwfs_parse_options: found read-only. Current flags: 0x%lX\n", sb->s_flags);
			sb->s_flags |= SB_RDONLY;
		}
		else if ( token == MGWFS_MNT_OPT_VERBOSE )
		{
			struct mgwfs_superblock *sbi = (struct mgwfs_superblock *)sb->s_fs_info;
			pr_info("mgwfs_parse_options: found verbose. Current flags: 0x%X\n", sbi->flags);
			sbi->flags |= MGWFS_SB_FLAG_VERBOSE;
		}
	}
	return 0;
}

static int mgwfs_fill_super(struct super_block *sb, void *data, int silent) {
    struct inode *root_inode;
    struct mgwfs_inode *root_mgwfs_inode;
    struct buffer_head *bh;
    struct mgwfs_superblock *mgwfs_sb;
    int ret = 0;

    bh = sb_bread(sb, MGWFS_SUPERBLOCK_BLOCK_NO);
    BUG_ON(!bh);
    mgwfs_sb = (struct mgwfs_superblock *)bh->b_data;
    if (unlikely(mgwfs_sb->magic != MGWFS_MAGIC)) {
        printk(KERN_ERR
               "The filesystem being mounted is not of type mgwfs. "
               "Magic number mismatch: %llu != %llu\n",
               mgwfs_sb->magic, (uint64_t)MGWFS_MAGIC);
        goto release;
    }
    if (unlikely(sb->s_blocksize != mgwfs_sb->blocksize)) {
        printk(KERN_ERR
               "mgwfs seem to be formatted with mismatching blocksize: %lu\n",
               sb->s_blocksize);
        goto release;
    }

    sb->s_magic = mgwfs_sb->magic;
    sb->s_fs_info = mgwfs_sb;
    sb->s_maxbytes = mgwfs_sb->blocksize;
    sb->s_op = &mgwfs_sb_ops;

    root_mgwfs_inode = mgwfs_get_mgwfs_inode(sb, MGWFS_ROOTDIR_INODE_NO);
    root_inode = new_inode(sb);
    if (!root_inode || !root_mgwfs_inode) {
        ret = -ENOMEM;
        goto release;
    }
    mgwfs_fill_inode(sb, root_inode, root_mgwfs_inode);
    inode_init_owner(&nop_mnt_idmap, root_inode, NULL, root_inode->i_mode);

    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root) {
        ret = -ENOMEM;
        goto release;
    }

    ret = mgwfs_parse_options(sb, data);
    if (ret) {
        pr_err("mgwfs_fill_super: Failed to parse options, error code: %d\n",
               ret);
    }
	if ( (mgwfs_sb->flags & MGWFS_SB_FLAG_VERBOSE) )
	{
		pr_info("mgwfs_fill_super: Finished. sb->flags=0x%lX, sbi->flags=0x%X\n", sb->s_flags, mgwfs_sb->flags);
	}
release:
    brelse(bh);
    return ret;
}

struct dentry *mgwfs_mount(struct file_system_type *fs_type,
                             int flags, const char *dev_name,
                             void *data) {
    struct dentry *ret;
    ret = mount_bdev(fs_type, flags, dev_name, data, mgwfs_fill_super);

    if (unlikely(IS_ERR(ret))) {
        printk(KERN_ERR "Error mounting mgwfs.\n");
    } else {
        printk(KERN_INFO "mgwfs is succesfully mounted on: %s\n",
               dev_name);
    }

    return ret;
}

void mgwfs_kill_superblock(struct super_block *sb) {
    printk(KERN_INFO
           "mgwfs superblock is destroyed. Unmount succesful.\n");
    kill_block_super(sb);
}

void mgwfs_put_super(struct super_block *sb) {
    return;
}

void mgwfs_save_sb(struct super_block *sb) {
    struct buffer_head *bh;
    struct mgwfs_superblock *mgwfs_sb = MGWFS_SB(sb);

    bh = sb_bread(sb, MGWFS_SUPERBLOCK_BLOCK_NO);
    BUG_ON(!bh);

    bh->b_data = (char *)mgwfs_sb;
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
}

int mgwfs_statfs(struct dentry *dirp, struct kstatfs *statp)
{
    struct super_block *sb = dirp->d_sb;
    struct mgwfs_superblock *sbi = MGWFS_SB(sb);

    statp->f_type = MGWFS_MAGIC;
    statp->f_bsize = MGWFS_DEFAULT_BLOCKSIZE;
    statp->f_blocks = sbi->fs_size/MGWFS_DEFAULT_BLOCKSIZE;
	statp->f_bfree = statp->f_blocks - sbi->data_block_count - sbi->inode_count;
    statp->f_bavail = sbi->data_block_table_size-sbi->data_block_count;
    statp->f_files = sbi->inode_table_size;
	statp->f_ffree = sbi->inode_table_size - sbi->inode_count;
    statp->f_namelen = MGWFS_FILENAME_MAXLEN;

    return 0;
}

