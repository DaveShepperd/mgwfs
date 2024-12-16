#include "kmgwfs.h"

DEFINE_MUTEX(mgwfs_sb_lock);

struct file_system_type mgwfs_fs_type = {
    .owner = THIS_MODULE,
    .name = "mgwfs",
    .mount = mgwfs_mount,
    .kill_sb = mgwfs_kill_superblock,
    .fs_flags = FS_REQUIRES_DEV,
};

const struct super_operations mgwfs_sb_ops = {
    .destroy_inode = mgwfs_destroy_inode,
    .put_super = mgwfs_put_super,
	.statfs = mgwfs_statfs,
};

const struct inode_operations mgwfs_inode_ops = {
    .create = mgwfs_create,
    .mkdir = mgwfs_mkdir,
    .lookup = mgwfs_lookup,
};

const struct file_operations mgwfs_dir_operations = {
    .owner = THIS_MODULE,
	.iterate_shared = mgwfs_readdir,
};

const struct file_operations mgwfs_file_operations = {
    .read = mgwfs_read,
    .write = mgwfs_write,
};

struct kmem_cache *mgwfs_inode_cache = NULL;

static int __init mgwfs_init(void)
{
    int ret;

    mgwfs_inode_cache = kmem_cache_create("mgwfs_inode_cache",
                                         sizeof(struct mgwfs_inode),
                                         0,
                                         (SLAB_RECLAIM_ACCOUNT /*| SLAB_MEM_SPREAD*/),
                                         NULL);
    if (!mgwfs_inode_cache) {
        return -ENOMEM;
    }

    ret = register_filesystem(&mgwfs_fs_type);
    if (likely(0 == ret)) {
        printk(KERN_INFO "Sucessfully registered mgwfs\n");
    } else {
        printk(KERN_ERR "Failed to register mgwfs. Error code: %d\n", ret);
    }

    return ret;
}

static void __exit mgwfs_exit(void)
{
    int ret;

    ret = unregister_filesystem(&mgwfs_fs_type);
    kmem_cache_destroy(mgwfs_inode_cache);

    if (likely(ret == 0)) {
        printk(KERN_INFO "Sucessfully unregistered mgwfs\n");
    } else {
        printk(KERN_ERR "Failed to unregister mgwfs. Error code: %d\n",
               ret);
    }
}

module_init(mgwfs_init);
module_exit(mgwfs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("accelazh");
