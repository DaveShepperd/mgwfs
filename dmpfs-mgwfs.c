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

int main(int argc, char *argv[])
{
	int fd, ii;
	ssize_t ret;
	const uint64_t welcome_inode_no = MGWFS_ROOTDIR_INODE_NO + 1;
	const uint64_t welcome_data_block_no_offset = MGWFS_ROOTDIR_DATA_BLOCK_NO_OFFSET + 1;
	struct mgwfs_superblock mgwfs_sb;
	char *cp, *inode_bitmap = NULL; /* [mgwfs_sb.blocksize]; */
	char *data_block_bitmap = NULL; /* [mgwfs_sb.blocksize]; */
	struct mgwfs_inode root_inode;
	struct mgwfs_inode welcome_mgwfs_inode;
	static const char welcome_body[] = "Welcome Hellofs!!\n";
	struct mgwfs_dir_record *root_dir_records = NULL;

	printf("__WORDSIZE=%d\n", __WORDSIZE);
	printf("sizeof char=%ld, int=%ld, short=%ld, long=%ld, *=%ld, long long=%ld\n",
					sizeof(char), sizeof(int), sizeof(short), sizeof(long), sizeof(char *), sizeof(long long));
	printf("sizeof unit8_t=%ld, uint16_t=%ld, uint32_t=%ld, uint64_t=%ld\n",
					sizeof(uint8_t), sizeof(uint16_t), sizeof(uint32_t), sizeof(uint64_t));
	printf("llval=%llX, lval=%lX, int=%X\n", (long long)0x1234, (long)0x4567, (int)0x89AB);

	do
	{
		struct stat st;
		ret = stat(argv[1],&st);
		if ( ret < 0 )
		{
			fprintf(stderr,"Unable to stat '%s': %s\n", argv[1], strerror(errno));
			break;
		}
		fd = open(argv[1], O_RDWR);
		if ( fd < 0 )
		{
			fprintf(stderr, "Error opening the '%s': %s\n", argv[1], strerror(errno));
			ret = -1;
			break;
		}
		memset(&mgwfs_sb, 0, sizeof(mgwfs_sb));
		printf("Reading superblock at file offset 0x%lX\n", lseek(fd, 0, SEEK_CUR));
		fflush(stdout);
		if ( sizeof(mgwfs_sb) != read(fd, &mgwfs_sb, sizeof(mgwfs_sb)) )
		{
			fprintf(stderr, "Failed to read superblock: %s\n", strerror(errno));
			ret = -2;
			break;
		}
		printf("superblock: Found:\n"
			   "sizeof(mgwfs_superblock)=%ld\n"
			   "version = %ld\n"
			   "magic = 0x%lX\n"
			   "blocksize = %ld\n"
			   "inode_table_size = %ld\n"
			   "inode_count = %ld\n"
			   "data_block_table_size = %ld\n"
			   "data_block_count = %ld\n"
			   "flags = 0x%X\n"
			   "misc = 0x%X\n"
			   "fs_size = %ld\n"
			   , sizeof(struct mgwfs_superblock)
			   , mgwfs_sb.version
			   , mgwfs_sb.magic
			   , mgwfs_sb.blocksize
			   , mgwfs_sb.inode_table_size
			   , mgwfs_sb.inode_count
			   , mgwfs_sb.data_block_table_size
			   , mgwfs_sb.data_block_count
			   , mgwfs_sb.flags
			   , mgwfs_sb.misc
			   , mgwfs_sb.fs_size
			  );
		fflush(stdout);
		if (   mgwfs_sb.version != 1
			 || mgwfs_sb.magic != MGWFS_MAGIC
			 || mgwfs_sb.blocksize != MGWFS_DEFAULT_BLOCKSIZE
			 || mgwfs_sb.inode_table_size != MGWFS_DEFAULT_INODE_TABLE_SIZE
			 || mgwfs_sb.inode_count != 2
			 || mgwfs_sb.data_block_table_size != MGWFS_DEFAULT_DATA_BLOCK_TABLE_SIZE
			 || mgwfs_sb.data_block_count != 2
			 || mgwfs_sb.flags != 0
			 || mgwfs_sb.misc != 0
			 || mgwfs_sb.fs_size != st.st_size
		   )
		{
			fprintf(stderr, "Failed superblock check. Expected:\n"
					"version = %ld\n"
					"magic = 0x%lX\n"
					"blocksize = %ld\n"
					"inode_table_size = %ld\n"
					"inode_count = %ld\n"
					"data_block_table_size = %ld\n"
					"data_block_count = %ld\n"
					"flags = 0x%X\n"
					"misc = 0x%X\n"
					"fs_size = %ld\n"
					, (uint64_t)1
					, (uint64_t)MGWFS_MAGIC
					, (uint64_t)MGWFS_DEFAULT_BLOCKSIZE
					, (uint64_t)MGWFS_DEFAULT_INODE_TABLE_SIZE
					, (uint64_t)2
					, (uint64_t)MGWFS_DEFAULT_DATA_BLOCK_TABLE_SIZE
					, (uint64_t)2
					, (uint32_t)0
					, (uint32_t)0
					, st.st_size
				   );
			ret = -3;
			break;
		}
		/* Skip to 1 byte beyond the end of superblock */
		if ( (off_t)-1 == lseek(fd, mgwfs_sb.blocksize, SEEK_SET) )
		{
			fprintf(stderr, "Failed to seek passed the superblock to %ld: %s\n", mgwfs_sb.blocksize, strerror(errno));
			ret = -4;
			break;
		}
		// read inode bitmap
		inode_bitmap = (char *)malloc(mgwfs_sb.blocksize);
		printf("Reading inode bitmap at at file offset 0x%lX\n", lseek(fd, 0, SEEK_CUR));
		fflush(stdout);
		if ( mgwfs_sb.blocksize != read(fd, inode_bitmap, mgwfs_sb.blocksize) )
		{
			fprintf(stderr, "Failed to read %ld byte inode bitmap: %s\n", mgwfs_sb.blocksize, strerror(errno));
			ret = -5;
			break;
		}
		cp = inode_bitmap;
		for ( ii = 0; ii < mgwfs_sb.blocksize; ++ii, ++cp )
		{
			if ( *cp )
			{
				if ( ii || *cp != 1 )
					break;
			}
		}
		if ( ii < mgwfs_sb.blocksize )
		{
			fprintf(stderr, "Failed inode bitmap. Expected byte %d to be 0x%02X. Found it 0x%02X\n", ii, ii ? 0 : 1, *cp & 0xFF);
			ret = -6;
			break;
		}
		// read data block bitmap
		data_block_bitmap = (char *)malloc(mgwfs_sb.blocksize);
		printf("Reading data block bitmap at file offset 0x%lX\n", lseek(fd, 0, SEEK_CUR));
		fflush(stdout);
		if ( mgwfs_sb.blocksize != read(fd, data_block_bitmap, mgwfs_sb.blocksize) )
		{
			fprintf(stderr, "Failed to read %ld byte data block bitmap: %s\n", mgwfs_sb.blocksize, strerror(errno));
			ret = -7;
			break;
		}
		cp = data_block_bitmap;
		for ( ii = 0; ii < mgwfs_sb.blocksize; ++ii, ++cp )
		{
			if ( *cp )
			{
				if ( ii || *cp != 1 )
					break;
			}
		}
		if ( ii < mgwfs_sb.blocksize )
		{
			fprintf(stderr, "Failed data block bitmap. Expected byte %d to be 0x%02X. Found it 0x%02X\n", ii, ii ? 0 : 1, *cp & 0xFF);
			ret = -8;
			break;
		}
		memset(&root_inode, 0, sizeof(root_inode));
		printf("Reading root inode at file offset 0x%lX\n", lseek(fd, 0, SEEK_CUR));
		fflush(stdout);
		if ( sizeof(root_inode) != read(fd, &root_inode, sizeof(root_inode)) )
		{
			fprintf(stderr, "Failed to read root inode: %s\n", strerror(errno));
			ret = -10;
			break;
		}
		printf("Found root inode:\n"
			   "mode = 0x%X\n"
			   "inode_no = %ld\n"
			   "data_block_no = %ld\n"
			   "dir_children_count = %ld\n"
			   , root_inode.mode
			   , root_inode.inode_no
			   , root_inode.data_block_no
			   , root_inode.dir_children_count
			  );
		fflush(stdout);
		if (    root_inode.mode != (S_IFDIR | S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)
			 || root_inode.inode_no != MGWFS_ROOTDIR_INODE_NO
			 || root_inode.data_block_no != MGWFS_DATA_BLOCK_TABLE_START_BLOCK_NO_HSB(&mgwfs_sb) + MGWFS_ROOTDIR_DATA_BLOCK_NO_OFFSET
			 || root_inode.dir_children_count != 1
		   )
		{
			fprintf(stderr, "Failed root inode check. Expected:\n"
					"mode = 0x%X\n"
					"inode_no = %ld\n"
					"data_block_no = %ld\n"
					"dir_children_count = %ld\n"
					"file_size = %ld\n"
					, (S_IFDIR | S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)
					, (uint64_t)MGWFS_ROOTDIR_INODE_NO
					, (uint64_t)MGWFS_DATA_BLOCK_TABLE_START_BLOCK_NO_HSB(&mgwfs_sb) + MGWFS_ROOTDIR_DATA_BLOCK_NO_OFFSET
					, (uint64_t)1
					, (uint64_t)0
				   );
			ret = -11;
			break;
		}

		printf("Reading %ld byte welcome inode at file offset 0x%lX\n", sizeof(welcome_mgwfs_inode), lseek(fd,0,SEEK_CUR));
		fflush(stdout);
		if (sizeof(welcome_mgwfs_inode)
				!= read(fd, &welcome_mgwfs_inode,
						 sizeof(welcome_mgwfs_inode))) {
			fprintf(stderr, "Failed to welcome inode: %s\n", strerror(errno));
			ret = -12;
			break;
		}
		printf("Found root inode:\n"
			   "mode = 0x%X\n"
			   "inode_no = %ld\n"
			   "data_block_no = %ld\n"
			   "file_size = %ld\n"
			   , welcome_mgwfs_inode.mode
			   , welcome_mgwfs_inode.inode_no
			   , welcome_mgwfs_inode.data_block_no
			   , welcome_mgwfs_inode.file_size
			  );
		fflush(stdout);
		if (    welcome_mgwfs_inode.mode != (S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH)
			 || welcome_mgwfs_inode.inode_no != welcome_inode_no
			 || welcome_mgwfs_inode.data_block_no != MGWFS_DATA_BLOCK_TABLE_START_BLOCK_NO_HSB(&mgwfs_sb) + welcome_data_block_no_offset
			 || welcome_mgwfs_inode.file_size != sizeof(welcome_body)
		   )
		{
			fprintf(stderr, "Failed welcome inode check. Expected:\n"
					"mode = 0x%lX\n"
					"inode_no = %ld\n"
					"data_block_no = %ld\n"
					"file_size = %ld\n"
					, (uint64_t)(S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH)
					, (uint64_t)welcome_inode_no
					, (uint64_t)MGWFS_DATA_BLOCK_TABLE_START_BLOCK_NO_HSB(&mgwfs_sb) + welcome_data_block_no_offset
					, (uint64_t)sizeof(welcome_body)
				   );
			ret = -11;
			break;
		}
		// read root inode data block
		if ( (off_t)-1 == lseek(fd,
								MGWFS_DATA_BLOCK_TABLE_START_BLOCK_NO_HSB(&mgwfs_sb) * mgwfs_sb.blocksize,
								SEEK_SET) )
		{
			fprintf(stderr, "Failed to seek to root inode data offset %ld: %s\n",
					MGWFS_DATA_BLOCK_TABLE_START_BLOCK_NO_HSB(&mgwfs_sb) * mgwfs_sb.blocksize,
					strerror(errno));
			ret = -9;
			break;
		}
		printf("Reading %ld byte root inode data at file offset 0x%lX\n", mgwfs_sb.blocksize, lseek(fd,0,SEEK_CUR));
		fflush(stdout);
		root_dir_records = (struct mgwfs_dir_record *)malloc(mgwfs_sb.blocksize);
		if (mgwfs_sb.blocksize != read(fd, root_dir_records, mgwfs_sb.blocksize)) {
			fprintf(stderr, "Failed to read root inode data: %s\n", strerror(errno));
			ret = -12;
			break;
		}
		printf("Contents of root dir records (potentially %ld entries):\n", mgwfs_sb.blocksize/sizeof(struct mgwfs_dir_record));
		for (ii=0; ii < mgwfs_sb.blocksize/sizeof(struct mgwfs_dir_record); ++ii)
		{
			if ( !root_dir_records[ii].inode_no || !root_dir_records[ii].filename[0] )
				continue;
			printf("\t%d: %ld %s\n", ii, root_dir_records[ii].inode_no, root_dir_records[ii].filename);
		}
		fflush(stdout);
	} while ( 0 );
	if ( fd >= 0 )
		close(fd);
	if ( inode_bitmap )
		free(inode_bitmap);
	if ( data_block_bitmap )
		free(data_block_bitmap);
	if ( root_dir_records )
		free(root_dir_records);
	return ret;
}
