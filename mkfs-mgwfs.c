#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "mgwfs.h"

#ifndef S_IFREG
#define S_IFREG 0100000
#endif
#ifndef S_IFDIR
#define S_IFDIR 0040000
#endif

int main(int argc, char *argv[]) {
    int fd, sts;
    ssize_t ret;
    uint64_t welcome_inode_no;
    uint64_t welcome_data_block_no_offset;
    struct stat st;
    // construct superblock
    struct mgwfs_superblock mgwfs_sb = {
        .version = 1,
        .magic = MGWFS_MAGIC,
        .blocksize = MGWFS_DEFAULT_BLOCKSIZE,
        .inode_table_size = MGWFS_DEFAULT_INODE_TABLE_SIZE,
        .inode_count = 2,
        .data_block_table_size = MGWFS_DEFAULT_DATA_BLOCK_TABLE_SIZE,
        .data_block_count = 2,
        .flags = 0,
        .misc = 0,
    };

    sts = stat(argv[1],&st);
    if ( sts < 0 )
    {
        fprintf(stderr,"Unable to stat '%s': %s\n", argv[1], strerror(errno));
        return -1;
    }
    mgwfs_sb.fs_size = st.st_size;
    
    fd = open(argv[1], O_RDWR);
    if (fd == -1) {
        perror("Error opening the device");
        return -1;
    }

    // construct inode bitmap
    char inode_bitmap[mgwfs_sb.blocksize];
    memset(inode_bitmap, 0, sizeof(inode_bitmap));
    inode_bitmap[0] = 1;

    // construct data block bitmap
    char data_block_bitmap[mgwfs_sb.blocksize];
    memset(data_block_bitmap, 0, sizeof(data_block_bitmap));
    data_block_bitmap[0] = 1;

    // construct root inode
    struct mgwfs_inode root_mgwfs_inode = {
        .mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH,
        .inode_no = MGWFS_ROOTDIR_INODE_NO,
        .data_block_no 
            = MGWFS_DATA_BLOCK_TABLE_START_BLOCK_NO_HSB(&mgwfs_sb)
                + MGWFS_ROOTDIR_DATA_BLOCK_NO_OFFSET,
        .dir_children_count = 1,
    };

    // construct welcome file inode
    char welcome_body[] = "Welcome Hellofs!!\n";
    welcome_inode_no = MGWFS_ROOTDIR_INODE_NO + 1;
    welcome_data_block_no_offset = MGWFS_ROOTDIR_DATA_BLOCK_NO_OFFSET + 1;
    struct mgwfs_inode welcome_mgwfs_inode = {
        .mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH,
        .inode_no = welcome_inode_no,
        .data_block_no 
            = MGWFS_DATA_BLOCK_TABLE_START_BLOCK_NO_HSB(&mgwfs_sb)
                + welcome_data_block_no_offset,
        .file_size = sizeof(welcome_body),
    };

    // construct root inode data block
    struct mgwfs_dir_record root_dir_records[] = {
		{
			.filename = "wel_helo.txt",
			.inode_no = welcome_inode_no,
		},
    };

    ret = 0;
    do {
        // write super block
        printf("Writing %ld byte superblock at file offset 0x%lX\n", sizeof(mgwfs_sb), lseek(fd,0,SEEK_CUR));
        if (sizeof(mgwfs_sb)
                != write(fd, &mgwfs_sb, sizeof(mgwfs_sb))) {
            ret = -1;
            break;
        }
        if ((off_t)-1
                == lseek(fd, mgwfs_sb.blocksize, SEEK_SET)) {
            ret = -2;
            break;
        }

        // write inode bitmap
        printf("Writing %ld byte inode bitmap at file offset 0x%lX\n", sizeof(inode_bitmap), lseek(fd,0,SEEK_CUR));
        if (sizeof(inode_bitmap)
                != write(fd, inode_bitmap, sizeof(inode_bitmap))) {
            ret = -3;
            break;
        }

        // write data block bitmap
        printf("Writing %ld byte data block bitmap at file offset 0x%lX\n", sizeof(data_block_bitmap), lseek(fd,0,SEEK_CUR));
        if (sizeof(data_block_bitmap)
                != write(fd, data_block_bitmap,
                         sizeof(data_block_bitmap))) {
            ret = -4;
            break;
        }

        // write root inode
        printf("Writing %ld byte root inode at file offset 0x%lX\n", sizeof(root_mgwfs_inode), lseek(fd,0,SEEK_CUR));
        if (sizeof(root_mgwfs_inode)
                != write(fd, &root_mgwfs_inode,
                         sizeof(root_mgwfs_inode))) {
            ret = -5;
            break;
        }

        // write welcome file inode
        printf("Writing %ld byte welcome inode at file offset 0x%lX\n", sizeof(welcome_mgwfs_inode), lseek(fd,0,SEEK_CUR));
        if (sizeof(welcome_mgwfs_inode)
                != write(fd, &welcome_mgwfs_inode,
                         sizeof(welcome_mgwfs_inode))) {
            ret = -6;
            break;
        }

        // write root inode data block
        if ((off_t)-1
                == lseek(
                    fd,
                    MGWFS_DATA_BLOCK_TABLE_START_BLOCK_NO_HSB(&mgwfs_sb)
                        * mgwfs_sb.blocksize,
                    SEEK_SET)) {
            ret = -7;
            break;
        }
        printf("Writing %ld byte root dir records (%ld entries) at file offset 0x%lX\n", sizeof(root_dir_records), sizeof(root_dir_records)/sizeof(struct mgwfs_dir_record), lseek(fd,0,SEEK_CUR));
        if (sizeof(root_dir_records) != write(fd, root_dir_records, sizeof(root_dir_records))) {
            ret = -8;
            break;
        }

        // write welcome file inode data block
        if ((off_t)-1
                == lseek(
                    fd,
                    (MGWFS_DATA_BLOCK_TABLE_START_BLOCK_NO_HSB(&mgwfs_sb)
                        + 1) * mgwfs_sb.blocksize,
                    SEEK_SET)) {
            ret = -9;
            break;
        }
        printf("Writing %ld byte welcome body at file offset 0x%lX\n", sizeof(welcome_body), lseek(fd,0,SEEK_CUR));
        if (sizeof(welcome_body) != write(fd, welcome_body,
                                          sizeof(welcome_body))) {
            ret = -10;
            break;
        }
    } while (0);

    close(fd);
    return ret;
}
