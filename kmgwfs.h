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

int mgwfs_create(struct mnt_idmap *, struct inode *,struct dentry *, umode_t, bool);
int mgwfs_atomic_open(struct inode *, struct dentry *, struct file *, unsigned open_flag, umode_t create_mode);
int mgwfs_unlink(struct inode *,struct dentry *);
/* int mgwfs_symlink(struct mnt_idmap *, struct inode *,struct dentry *, const char *); */
int mgwfs_mkdir(struct mnt_idmap *, struct inode *,struct dentry *, umode_t);
int mgwfs_rmdir(struct inode *,struct dentry *);
/* int mgwfs_mknod(struct mnt_idmap *, struct inode *,struct dentry *, umode_t,dev_t); */
int mgwfs_rename(struct mnt_idmap *, struct inode *, struct dentry *, struct inode *, struct dentry *, unsigned int);
struct dentry *mgwfs_lookup(struct inode *parent_inode,
                               struct dentry *child_dentry,
                               unsigned int flags);

int mgwfs_readdir(struct file *filp, struct dir_context *dirCtx /*void *dirent, filldir_t filldir*/);

ssize_t mgwfs_read(struct file * filp, char __user * buf, size_t len,
                      loff_t * ppos);
loff_t mgwfs_llseek(struct file *filp, loff_t offset, int whence);

ssize_t mgwfs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos);

extern struct kmem_cache *mgwfs_inode_cache;

/* Helper functions */

// To translate VFS superblock to mgwfs superblock
static inline MgwfsSuper_t *MGWFS_SB(struct super_block *sb) {
    return (MgwfsSuper_t *)sb->s_fs_info;
}
static inline MgwfsInode_t *MGWFS_INODE(struct inode *inode) {
    return (MgwfsInode_t *)inode->i_private;
}

void mgwfs_save_sb(struct super_block *sb);

// functions to operate inode
void mgwfs_fill_inode(struct super_block *sb, struct inode *inode, MgwfsInode_t *mgwfs_inode, const char *fName);
//int mgwfs_alloc_mgwfs_inode(struct super_block *sb, uint64_t *out_inode_no, umode_t mode);
void mgwfs_save_mgwfs_inode(struct super_block *sb, MgwfsInode_t *inode);
MgwfsInode_t *mgwfs_get_mgwfs_inode(struct super_block *sb, uint32_t inode_no, int generation, const char *fileName );

int mgwfs_statfs(struct dentry *dirp, struct kstatfs *statp);

#endif /*__KMGWFS_H__*/
