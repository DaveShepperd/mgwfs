#ifndef __KMGWFS_H__
#define __KMGWFS_H__

/* kmgwfs.h defines symbols to work in kernel space */

#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/namei.h>
#include <linux/module.h>
#include <linux/parser.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/version.h>
#include <linux/statfs.h>

#include "mgwfs.h"

/* Declare operations to be hooked to VFS */

extern struct file_system_type mgwfs_fs_type;
extern const struct super_operations mgwfs_sb_ops;
extern const struct inode_operations mgwfs_inode_ops;
extern const struct file_operations mgwfs_dir_operations;
extern const struct file_operations mgwfs_file_operations;

struct dentry *mgwfs_mount(struct file_system_type *fs_type,
                              int flags, const char *dev_name,
                              void *data);
void mgwfs_kill_superblock(struct super_block *sb);

void mgwfs_destroy_inode(struct inode *inode);
// void mgwfs_put_super(struct super_block *sb);

//int mgwfs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry,
//                    umode_t mode, bool excl);
struct dentry *mgwfs_lookup(struct inode *parent_inode,
                               struct dentry *child_dentry,
                               unsigned int flags);
//int mgwfs_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry,
//                   umode_t mode);

int mgwfs_readdir(struct file *filp, struct dir_context *dirCtx /*void *dirent, filldir_t filldir*/);

ssize_t mgwfs_read(struct file * filp, char __user * buf, size_t len,
                      loff_t * ppos);
loff_t mgwfs_llseek(struct file *filp, loff_t offset, int whence);

//ssize_t mgwfs_write(struct file * filp, const char __user * buf, size_t len,
//                       loff_t * ppos);

extern struct kmem_cache *mgwfs_inode_cache;

/* Helper functions */

// To translate VFS superblock to mgwfs superblock
static inline MgwfsSuper_t *MGWFS_SB(struct super_block *sb) {
    return (MgwfsSuper_t *)sb->s_fs_info;
}
static inline MgwfsInode_t *MGWFS_INODE(struct inode *inode) {
    return (MgwfsInode_t *)inode->i_private;
}

#if 0
static inline uint64_t MGWFS_INODES_PER_BLOCK(struct super_block *sb) {
    struct mgwfs_superblock *mgwfs_sb;
    mgwfs_sb = MGWFS_SB(sb);
    return MGWFS_INODES_PER_BLOCK_HSB(mgwfs_sb);
}

// Given the inode_no, calcuate which block in inode table contains the corresponding inode
static inline uint64_t MGWFS_INODE_BLOCK_OFFSET(struct super_block *sb, uint64_t inode_no) {
    struct mgwfs_superblock *mgwfs_sb;
    mgwfs_sb = MGWFS_SB(sb);
    return inode_no / MGWFS_INODES_PER_BLOCK_HSB(mgwfs_sb);
}
static inline uint64_t MGWFS_INODE_BYTE_OFFSET(struct super_block *sb, uint64_t inode_no) {
    struct mgwfs_superblock *mgwfs_sb;
    mgwfs_sb = MGWFS_SB(sb);
    return (inode_no % MGWFS_INODES_PER_BLOCK_HSB(mgwfs_sb)) * sizeof(struct mgwfs_inode);
}

static inline uint64_t MGWFS_DIR_MAX_RECORD(struct super_block *sb) {
    struct mgwfs_superblock *mgwfs_sb;
    mgwfs_sb = MGWFS_SB(sb);
    return mgwfs_sb->blocksize / sizeof(struct mgwfs_dir_record);
}

// From which block does data blocks start
static inline uint64_t MGWFS_DATA_BLOCK_TABLE_START_BLOCK_NO(struct super_block *sb) {
    struct mgwfs_superblock *mgwfs_sb;
    mgwfs_sb = MGWFS_SB(sb);
    return MGWFS_DATA_BLOCK_TABLE_START_BLOCK_NO_HSB(mgwfs_sb);
}

void mgwfs_save_sb(struct super_block *sb);
#endif

// functions to operate inode
void mgwfs_fill_inode(struct super_block *sb, struct inode *inode, MgwfsInode_t *mgwfs_inode, const char *fName);
int mgwfs_alloc_mgwfs_inode(struct super_block *sb, uint64_t *out_inode_no);
MgwfsInode_t *mgwfs_get_mgwfs_inode(struct super_block *sb, uint32_t inode_no, int generation, const char *fileName );
#if 0
void mgwfs_save_mgwfs_inode(struct super_block *sb, MgwfsInode_t *inode);
int mgwfs_add_dir_record(struct super_block *sb, struct inode *dir, struct dentry *dentry, struct inode *inode);
int mgwfs_alloc_data_block(struct super_block *sb, uint64_t *out_data_block_no);
int mgwfs_create_inode(struct inode *dir, struct dentry *dentry, umode_t mode);
#endif
int mgwfs_statfs(struct dentry *dirp, struct kstatfs *statp);

#endif /*__KMGWFS_H__*/
