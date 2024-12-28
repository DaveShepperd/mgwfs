#define _LARGEFILE64_SOURCE 
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#define FSYS_FEATURES (FSYS_FEATURES_CMTIME|FSYS_FEATURES_JOURNAL)
#include "agcfsys.h"

#ifndef S_IFREG
#define S_IFREG 0100000
#endif
#ifndef S_IFDIR
#define S_IFDIR 0040000
#endif

typedef struct part
{
    uint8_t status;
    uint8_t st_head;
    uint16_t st_sectcyl;
    uint8_t type;
    uint8_t en_head;
    uint16_t en_sectcyl;
    uint32_t abs_sect;
    uint32_t num_sects;
} Partition;

struct bootsect
{
    uint8_t jmp[3];          /* 0x000 x86 jump */
    uint8_t oem_name[8];     /* 0x003 OEM name */
    uint8_t bps[2];          /* 0x00B bytes per sector */
    uint8_t sects_clust;     /* 0x00D sectors per cluster */
    uint16_t num_resrv;      /* 0x00E number of reserved sectors */
    uint8_t num_fats;        /* 0x010 number of FATs */
    uint8_t num_roots[2];        /* 0x011 number of root directory entries */
    uint8_t total_sects[2];      /* 0x013 total sectors in volume */
    uint8_t media_desc;      /* 0x015 media descriptor */
    uint16_t sects_fat;      /* 0x016 sectors per FAT */
    uint16_t sects_trk;      /* 0x018 sectors per track */
    uint16_t num_heads;      /* 0x01A number of heads */
    uint32_t num_hidden;     /* 0x01C number of hidden sectors */
    uint32_t total_sects_vol;    /* 0x020 total sectors in volume */
    uint8_t drive_num;       /* 0x024 drive number */
    uint8_t reserved0;       /* 0x025 unused */
    uint8_t boot_sig;        /* 0x026 extended boot signature */
    uint8_t vol_id[4];       /* 0x027 volume ID */
    uint8_t vol_label[11];       /* 0x02B volume label */
    uint8_t reserved1[8];        /* 0x036 unused */
    uint8_t bootstrap[384];      /* 0x03E boot code */
    Partition parts[4];     /* 0x1BE partition table */
    uint16_t end_sig;        /* 0x1FE end signature */
} __attribute__ ((packed));

typedef struct bootsect BootSect;
static BootSect bootSect;
off64_t baseSector;
#define VERBOSE_HOME	(1<<0)	/* display home block */
#define VERBOSE_HEADERS	(1<<1)	/* display file headers */
#define VERBOSE_READ	(1<<2)	/* display read requests */
#define VERBOSE_INDEX	(1<<3)	/* display index.sys file */
#define VERBOSE_FREEMAP	(1<<4)	/* display freemap file */
#define VERBOSE_DMPROOT	(1<<5)	/* dump root directory contents */
#define VERBOSE_ITERATE	(1<<6)	/* iterate directory tree */
static int verbose=VERBOSE_HOME;	/* Default to reading home block(s) */

static void displayFileHeader(FILE *outp, FsysHeader *fhp, int retrievalsToo)
{
	FsysRetPtr *rp;
	int ii;
	
	rp = fhp->pointers[0];
	fprintf(outp,  "    id:        0x%X\n"
			       "    size:      %d\n"
				   "    clusters:  %d\n"
				   "    generation:%d\n"
				   "    type:      %d\n"
				   "    flags:     0x%04X\n"
				   "    ctime:     %d\n"
				   "    mtime:     %d\n"
			, fhp->id
			, fhp->size
			, fhp->clusters
			, fhp->generation
			, fhp->type
			, fhp->flags
			, fhp->ctime
			, fhp->mtime
			);
	for (ii=0; ii < FSYS_MAX_FHPTRS; ++ii, ++rp )
	{
		if ( !rp->start || !rp->nblocks )
			break;
	}
	rp = fhp->pointers[0];
	fprintf(outp, "    pointers (%d of %ld):", ii, FSYS_MAX_FHPTRS);
	if ( retrievalsToo )
	{
		for (ii=0; ii < FSYS_MAX_FHPTRS; ++ii, ++rp )
		{
			if ( !rp->start || !rp->nblocks )
				break;
			fprintf(outp, " 0x%X/%d", rp->start, rp->nblocks);
		}
	}
	else
		fprintf(outp, " 0x%X/%d%s", rp->start, rp->nblocks, (fhp->pointers[1]->start && fhp->pointers[1]->nblocks) ? " ...":"");
	fprintf(outp, "\n");
	fflush(outp);
}

static void displayHomeBlock(FILE *outp, const FsysHomeBlock *homeBlkp, uint32_t cksum)
{
	fprintf(outp,
		   "    id:        0x%X\n"
		   "    hb_minor:  %d\n"
		   "    hb_major:  %d\n"
		   "    hb_size:   %d\n"
		   "    fh_minor:  %d\n"
		   "    fh_major:  %d\n"
		   "    fh_size:   %d\n"
		   "    efh_minor: %d\n"
		   "    efh_major: %d\n"
		   "    efh_size:  %d\n"
		   "    efh_ptrs:  %d\n"
		   "    rp_minor:  %d\n"
		   "    rp_major:  %d\n"
		   "    rp_size:   %d\n"
		   "    cluster:   %d\n"
		   "    maxalts:   %d\n"
		   ,homeBlkp->id
		   ,homeBlkp->hb_minor
		   ,homeBlkp->hb_major
		   ,homeBlkp->hb_size
		   ,homeBlkp->fh_minor
		   ,homeBlkp->fh_major
		   ,homeBlkp->fh_size
		   ,homeBlkp->efh_minor
		   ,homeBlkp->efh_major
		   ,homeBlkp->efh_size
		   ,homeBlkp->efh_ptrs
		   ,homeBlkp->rp_minor
		   ,homeBlkp->rp_major
		   ,homeBlkp->rp_size
		   ,homeBlkp->cluster
		   ,homeBlkp->maxalts
		   );
	printf(    "    def_extend:%d\n"
			   "    ctime:     %d\n"
			   "    mtime:     %d\n"
			   "    atime:     %d\n"
			   "    btime:     %d\n"
			   "    chksum:    0x%X (computed 0x%X)\n"
			   "    features:  0x%08X\n"
			   "    options:   0x%08X\n"
			   "    index[]:   0x%08X,0x%08X,0x%08X\n"
			   "    boot[]:    0x%08X,0x%08X,0x%08X\n"
			   "    max_lba:   0x%08X\n"
			   "    upd_flag:  %d\n"
			   , homeBlkp->def_extend
			   , homeBlkp->ctime
			   , homeBlkp->mtime
			   , homeBlkp->atime
			   , homeBlkp->btime
			   , homeBlkp->chksum, cksum
			   , homeBlkp->features
			   , homeBlkp->options
			   , homeBlkp->index[0],homeBlkp->index[1],homeBlkp->index[2]
			   , homeBlkp->boot[0],homeBlkp->boot[1],homeBlkp->boot[2]
			   , homeBlkp->max_lba
			   , homeBlkp->upd_flag
			   );
	printf("    boot1[]:   0x%08X, 0x%08X, 0x%08X\n"
		   "    boot2[]:   0x%08X, 0x%08X, 0x%08X\n"
		   "    boot3[]:   0x%08X, 0x%08X, 0x%08X\n"
		   "    journal[]: 0x%08X, 0x%08X, 0x%08X\n"
		   ,homeBlkp->boot1[0], homeBlkp->boot1[1], homeBlkp->boot1[2]
		   ,homeBlkp->boot2[0], homeBlkp->boot2[1], homeBlkp->boot2[2]
		   ,homeBlkp->boot3[0], homeBlkp->boot3[1], homeBlkp->boot3[2]
		   ,homeBlkp->journal[0], homeBlkp->journal[1], homeBlkp->journal[2]
		   );
	fflush(outp);
}

static int getHomeBlock(int fd, uint32_t *lbas, FsysHomeBlock *homeBlkp, off64_t maxHb, off64_t sizeInSectors, uint32_t *ckSumP)
{
	FsysHomeBlock lclHomes[FSYS_MAX_ALTS], *lclHome=lclHomes;
	int ii, good=0, match=0;
	ssize_t sts;
	off64_t sector;
	int jj;
	uint32_t options;
	uint32_t *csp,cksum;
	
	memset(lclHomes,0,sizeof(lclHomes));
	*ckSumP = 0;
	for (ii=0; ii < FSYS_MAX_ALTS; ++ii, ++lclHome, ++lbas)
	{
		sector = *lbas+baseSector;
		if ( (verbose&VERBOSE_HOME) )
			printf("Attempting to read home block at sector 0x%lX\n", sector);
		if ( lseek64(fd,sector*512,SEEK_SET) == (off64_t)-1 )
		{
			fprintf(stderr,"Failed to seek to sector 0x%lX: %s\n", sector, strerror(errno));
			continue;
		}
		sts = read(fd, lclHome, sizeof(FsysHomeBlock));
		if ( sts != sizeof(FsysHomeBlock) )
		{
			fprintf(stderr,"Failed to read %ld byte home block at sector 0x%lX: %s\n", sizeof(FsysHomeBlock), sector, strerror(errno));
			continue;
		}
		cksum = 0;
		csp = (uint32_t *)lclHome;
		for (jj=0; jj < sizeof(FsysHomeBlock)/sizeof(uint32_t); ++jj)
			cksum += *csp++;
		options = lclHome->features & lclHome->options & (FSYS_FEATURES_CMTIME | FSYS_FEATURES_EXTENSION_HEADER | FSYS_FEATURES_ABTIME);
		if (    lclHome->id == FSYS_ID_HOME
			 && lclHome->rp_major == 1
			 && lclHome->rp_minor == 1
			 && !cksum
			 && options == FSYS_FEATURES_CMTIME
			 && lclHome->fh_size == 504
			 && lclHome->maxalts == FSYS_MAX_ALTS
		   )
		{
			good |= 1<<ii;
			*ckSumP = cksum;
		}
		else
		{
			fprintf(stderr,"Home block %d is not what is expected:\n", ii);
			displayHomeBlock(stderr, lclHome, cksum);
			continue;
		}
		if ( !ii )
		{
			match = 1;
		}
		else
		{
			if ( !memcmp(lclHomes+0,lclHome,sizeof(FsysHomeBlock)) )
				match |= 1<<ii;
			else
				fprintf(stderr,"Header %d does not match header 0\n", ii);
		}
	}
	if ( !good )
	{
		memset(homeBlkp,0,sizeof(FsysHomeBlock));
		return 0;
	}
	lclHome = lclHomes;
	if ( !(good&1) )
	{
		++lclHome;
		if ( !(good&2) )
			++lclHome;
	}
	memcpy(homeBlkp,lclHome,sizeof(FsysHomeBlock));
	return good;
}

static int getFileHeader(const char *title, int fd, uint32_t id, uint32_t lbas[FSYS_MAX_ALTS], FsysHeader *fhp)
{
	FsysHeader lclHdrs[FSYS_MAX_ALTS], *lclFhp=lclHdrs;
	int ii, good=0, match=0;
	ssize_t sts;
	off64_t sector;
	
	memset(lclHdrs,0,sizeof(lclHdrs));
	for (ii=0; ii < FSYS_MAX_ALTS; ++ii, ++lclFhp)
	{
		sector = lbas[ii]+baseSector;
		if ( (verbose&VERBOSE_HEADERS) )
			printf("Attempting to read file header for '%s' at sector 0x%lX\n", title, sector);
		if ( lseek64(fd,sector*512,SEEK_SET) == (off64_t)-1 )
		{
			fprintf(stderr,"Failed to seek to sector 0x%lX: %s\n", sector, strerror(errno));
			continue;
		}
		sts = read(fd, lclFhp, sizeof(FsysHeader));
		if ( sts != sizeof(FsysHeader) )
		{
			fprintf(stderr,"Failed to read %ld byte file header at sector 0x%lX: %s\n", sizeof(FsysHeader), sector, strerror(errno));
			continue;
		}
		if ( lclFhp->id != id )
		{
			fprintf(stderr, "Sector at 0x%lX is not a file header:\n", sector);
			displayFileHeader(stderr,lclFhp,0);
			continue;
		}
		good |= 1<<ii;
		if ( !ii )
		{
			match = 1;
		}
		else
		{
			if ( !memcmp(lclHdrs,lclFhp,sizeof(FsysHeader)) )
				match |= 1<<ii;
			else
				fprintf(stderr,"Header %d does not match header 0\n", ii);
		}
	}
	if ( good )
	{
		lclFhp = lclHdrs;
		if ( !(good&1) )
		{
			++lclFhp;
			if ( !(good&2) )
				++lclFhp;
		}
		memcpy(fhp,lclFhp,sizeof(FsysHeader));
	}
	else
	{
		memset(fhp,0,sizeof(FsysHeader));
		return 0;
	}
	return 1;
}

static int readFile(const char *title, int fd, uint8_t *dst, int bytes, FsysRetPtr *retPtr)
{
	off64_t sector;
	int ptrIdx=0, retSize=0, blkLimit;
	ssize_t rdSts, limit;
	
	while ( retSize < bytes )
	{
		if ( !retPtr->start || !retPtr->nblocks  )
		{
			fprintf(stderr,"Empty retrieval pointer at retIdx %d while reading '%s'\n", ptrIdx, title);
			return -1;
		}
		sector = retPtr->start;
		blkLimit = retPtr->nblocks;
		limit = bytes - retSize;
		if ( blkLimit*BYTES_PER_SECTOR > limit )
			blkLimit = ((blkLimit*BYTES_PER_SECTOR-limit)+BYTES_PER_SECTOR-1)/512;
		if ( (verbose&VERBOSE_READ) )
		{
			printf("Attempting to read %ld bytes for %s. ptrIdx=%d, sector=0x%X, nblocks=%d (limited blocks=%d)\n",
			   limit, title, ptrIdx, retPtr->start, retPtr->nblocks, blkLimit);
		}
		if ( lseek64(fd,(sector+baseSector)*BYTES_PER_SECTOR,SEEK_SET) == (off64_t)-1 )
		{
			fprintf(stderr,"Failed to seek to sector 0x%lX: %s\n", sector, strerror(errno));
			return -1;
		}
		if ( limit > retPtr->nblocks*BYTES_PER_SECTOR )
		{
			limit = retPtr->nblocks*BYTES_PER_SECTOR;
			++retPtr;
			++ptrIdx;
		}
		rdSts = read(fd, dst+retSize, limit);
		if ( rdSts != limit )
		{
			fprintf(stderr,"Failed to read %ld bytes for %s. Instead got %ld: %s\n", limit, title, rdSts, strerror(errno));
			return -1;
		}
		retSize += rdSts;
	}
	return retSize;
}

static void dumpIndex(uint32_t *indexBase, int bytes)
{
	int ii=0;
	uint32_t *index = indexBase;
	const char *Titles[] = 
	{
		"index.sys",
		"freemap.sys",
		"rootdir.sys",
		"journal.sys"
	};
	printf("Contents of index.sys:\n");
	while ( index < indexBase+(bytes+sizeof(uint32_t)-1)/sizeof(uint32_t) )
	{
		printf("    %5d: 0x%08X 0x%08X 0x%08X", ii, index[0], index[1], index[2]);
		if ( ii < 4 )
			printf(" (%s)", Titles[ii]);
		printf("\n");
		++ii;
		index += 3;
	}
}

static void dumpFreemap(FsysRetPtr *rpBase, int bytes )
{
	FsysRetPtr *rp = rpBase;
	int ii=0;
	
	printf("Contents of freemap.sys:\n");
	while ( rp < rpBase+(bytes+sizeof(FsysRetPtr)-1)/sizeof(FsysRetPtr) )
	{
		printf("    %5d: 0x%08X/%d\n", ii, rp->start, rp->nblocks);
		++ii;
		++rp;
	}
}

void dumpDir(uint8_t *dirBase, int bytes, int fd, uint32_t *indexSys )
{
	uint8_t *dir = dirBase;
	int ii=0;
	FsysHeader hdr;
	
	printf("Contents of rootdir.sys:\n");
	while ( dir < dirBase+bytes )
	{
		int txtLen;
		uint8_t gen;
		uint32_t fid;

		fid = (dir[2]<<16)|(dir[1]<<8)|dir[0];
		if ( fid == 0 )
			break;
		dir += 3;
		gen = *dir++;
		txtLen = *dir++;
		if ( !txtLen )
			txtLen = 256;
		printf("    %5d: 0x%08X 0x%02X %3d %s\n", ii, fid, gen, txtLen, dir );
		if ( fd > 0 && indexSys )
		{
			if ( getFileHeader((char *)dir, fd, FSYS_ID_HEADER, indexSys + fid * FSYS_MAX_ALTS, &hdr) )
				displayFileHeader(stdout,&hdr,1);
		}
		++ii;
		dir += txtLen;
	}
}

int iterateDir(int nest, uint8_t *dirBase, int bytes, int fd, uint32_t *indexSys)
{
	uint8_t *dir = dirBase;
	int ii=0;
	FsysHeader hdr;
	int ret=0;
	
	if ( nest > 16 )
	{
		fprintf(stderr,"Directory nest at 16 is too deep\n");
		return 1;
	}
	while ( dir < dirBase + bytes )
	{
		int txtLen;
		uint32_t fid;
		uint8_t gen;
		
		fid = (dir[2]<<16)|(dir[1]<<8)|dir[0];
		if ( fid == 0 )
			break;
		dir += 3;
		gen = *dir++;
		txtLen = *dir++;
		if ( !txtLen )
			txtLen = 256;
		if ( dir[0] != '.' )	/* skip all files starting with a dot */
		{
			if ( getFileHeader((char *)dir, fd, FSYS_ID_HEADER, indexSys + fid * FSYS_MAX_ALTS, &hdr) && hdr.generation  == gen )
			{
				if ( hdr.type == FSYS_TYPE_DIR  )
				{
					uint8_t *fileBuff;
					printf("%*s%5d: 0x%08X DIR 0x%08X/0x%08X 0x%02X %s\n", nest*4," ",ii, fid, hdr.size, hdr.clusters, txtLen, dir );
					if ( (verbose&VERBOSE_HEADERS) )
						displayFileHeader(stdout, &hdr, 1);
					fileBuff = (uint8_t*)malloc(hdr.clusters*512);
					if ( fileBuff )
					{
						if ( readFile((char *)dir, fd, fileBuff, hdr.size, hdr.pointers[0]) == hdr.size)
						{
							ret |= iterateDir(nest+1,fileBuff,hdr.size,fd,indexSys);
						}
						free(fileBuff);
					}
				}
				else
					printf("%*s%5d: 0x%08X REG 0x%08X/0x%08X 0x%02X %s\n", nest*4," ",ii, fid, hdr.size, hdr.clusters,txtLen, dir );
			}
			else
				printf("%*s%5d: 0x%08X REG 0x%02X %s *** FAILED to read file header\n", nest*4," ",ii, fid, txtLen, dir );
		}
		++ii;
		dir += txtLen;
	}
	return ret;
}

static int helpEm(void)
{
	printf("Usage: dmpfs-mgwfs [-v n] image\n"
		   "Where:\n"
		   "-v  = bit mask of verbose modes:\n"
		   "      1 = display home block\n"
		   "      2 = display file headers\n"
		   "      4 = display attempts at file reads\n"
		   "      8 = display contents of index.sys file\n"
		   "     16 = display contents of freemap.sys file\n"
		   "     32 = display contents of rootdir.sys file\n"
		   "     64 = display directory tree\n"
		   "image - path to filesystem\n"
		   );
	return 1;
}

int main(int argc, char *argv[])
{
	int fd, ii, cc;
	ssize_t ret;
	const char *inpName;
	char *endp;
	FsysHomeBlock homeBlk;
	uint32_t homeLbas[FSYS_MAX_ALTS], ckSum;
	FsysHeader indexFileHeader, freemapFileHeader, dirFileHeader, bootFileHeader, journalFileHeader;
	uint32_t *indexFileContents=NULL;
	FsysRetPtr *freemapFileContents=NULL;
	uint8_t *dirFileContents=NULL;
	
	while ( (cc=getopt(argc,argv,"v:")) != -1 )
	{
		switch (cc)
		{
		case 'v':
			endp = NULL;
			verbose = strtol(optarg,&endp,0);
			if ( verbose < 0 || !endp || *endp )
			{
				fprintf(stderr,"Invalid argument for -v: '%s'\n", optarg);
				return helpEm();
			}
			break;
		default:
			return helpEm();
		}
	}
	if ( optind >= argc )
	{
		fprintf(stderr,"No input file provided\n");
		return helpEm();
	}
	inpName = argv[optind];
	if ( verbose )
	{
		printf("__WORDSIZE=%d\n", __WORDSIZE);
		printf("sizeof char=%ld, int=%ld, short=%ld, long=%ld, *=%ld, long long=%ld\n",
						sizeof(char), sizeof(int), sizeof(short), sizeof(long), sizeof(char *), sizeof(long long));
		printf("sizeof unit8_t=%ld, uint16_t=%ld, uint32_t=%ld, uint64_t=%ld\n",
						sizeof(uint8_t), sizeof(uint16_t), sizeof(uint32_t), sizeof(uint64_t));
		printf("llval=%llX, lval=%lX, int=%X\n", (long long)0x1234, (long)0x4567, (int)0x89AB);
		printf("FSYS_FEATURES=0x%08X, FSYS_OPTIONS=0x%08X, FSYS_MAX_ALTS=%d, FSYS_MAX_FHPTRS=%ld\n", FSYS_FEATURES, FSYS_OPTIONS, FSYS_MAX_ALTS, FSYS_MAX_FHPTRS);
	}
	do
	{
		struct stat st;
		off64_t maxHb;
		off64_t sizeInSectors;
		
		if ( FSYS_MAX_ALTS != 3 )
		{
			fprintf(stderr, "FSYS_MAX_ALTS=%d. Application has to be built with it set to 3.\n", FSYS_MAX_ALTS);
			ret = -1;
			break;
		}
		ret = stat(inpName, &st);
		if ( ret < 0 )
		{
			fprintf(stderr,"Unable to stat '%s': %s\n", inpName, strerror(errno));
			break;
		}
		fd = open(inpName, O_RDONLY);
		if ( fd < 0 )
		{
			fprintf(stderr, "Error opening the '%s': %s\n", inpName, strerror(errno));
			ret = -1;
			break;
		}
		
		sizeInSectors = st.st_size/512;
		maxHb = sizeInSectors > FSYS_HB_RANGE ? FSYS_HB_RANGE:sizeInSectors;
		if ( (verbose&VERBOSE_HOME) )
		{
			printf("File size 0x%lX, maxSector=0x%lX, maxHb=0x%lX\n", st.st_size, sizeInSectors, maxHb);
			printf("Attempting to read a partition table that might be present\n");
		}
		if ( (sizeof(bootSect) != read(fd,&bootSect,sizeof(bootSect))) )
		{
			fprintf(stderr, "Failed to read boot sector: %s\n", strerror(errno));
			ret = -2;
			break;
		}
		for (ii=0; ii < 4; ++ii)
		{
			if ( bootSect.parts[ii].status == 0x80 && bootSect.parts[ii].type == 0x8f )
			{
				baseSector = bootSect.parts[ii].abs_sect;
				sizeInSectors = bootSect.parts[ii].num_sects;
				maxHb = sizeInSectors > FSYS_HB_RANGE ? FSYS_HB_RANGE:sizeInSectors;
				if ( (verbose&VERBOSE_HOME) )
					printf("Found an agc fsys partition in partition %d. baseSector=0x%lX, numSectors=0x%lX, maxHb=0x%lX\n", ii, baseSector, sizeInSectors, maxHb);
				break;
			}
		}
		if ( (verbose&VERBOSE_HOME) && ii >= 4 )
			printf("No parition table found\n");
		for (ii=0; ii < FSYS_MAX_ALTS; ++ii)
			homeLbas[ii] = FSYS_HB_ALG(ii, maxHb);
		if ( !getHomeBlock(fd,homeLbas,&homeBlk,maxHb,sizeInSectors,&ckSum) )
		{
			ret = -1;
			break;
		}
		if ( (verbose&VERBOSE_HOME) )
		{
			printf("Home block:\n");
			displayHomeBlock(stdout,&homeBlk,ckSum);
		}
		if ( getFileHeader("index.sys", fd, FSYS_ID_INDEX, homeBlk.index, &indexFileHeader) && (verbose&VERBOSE_HEADERS) )
			displayFileHeader(stdout,&indexFileHeader,1);
		else
			ret = -1;
		if ( getFileHeader("boot0", fd, FSYS_ID_HEADER, homeBlk.boot, &bootFileHeader) && (verbose & VERBOSE_HEADERS) )
			displayFileHeader(stdout,&bootFileHeader,1);
		else
			ret = -1;
		indexFileContents = (uint32_t *)calloc(indexFileHeader.clusters*512,1);
		if ( readFile("index.sys", fd, (uint8_t*)indexFileContents, indexFileHeader.size, indexFileHeader.pointers[0]) < 0 )
		{
			fprintf(stderr,"Failed to read index.sys file\n");
			ret = -1;
			break;
		}
		if ( (verbose & VERBOSE_INDEX) )
			dumpIndex(indexFileContents, indexFileHeader.size);
		if ( getFileHeader("freemap.sys", fd, FSYS_ID_HEADER, indexFileContents+FSYS_INDEX_FREE*FSYS_MAX_ALTS,&freemapFileHeader ) )
		{
			if ( (verbose & VERBOSE_HEADERS) )
				displayFileHeader(stdout, &freemapFileHeader, 1);
		}
		if ( indexFileContents[FSYS_FEATURES_JOURNAL*FSYS_MAX_ALTS] )
		{
			if ( getFileHeader("journal.sys", fd, FSYS_ID_HEADER, indexFileContents+FSYS_INDEX_JOURNAL*FSYS_MAX_ALTS, &journalFileHeader) && (verbose&VERBOSE_HEADERS) )
				displayFileHeader(stdout,&journalFileHeader,1);
		}
		freemapFileContents = (FsysRetPtr *)calloc(freemapFileHeader.clusters*512,1);
		if ( readFile("freemap.sys", fd, (uint8_t*)freemapFileContents, freemapFileHeader.size, freemapFileHeader.pointers[0]) < 0 ) 
		{
			fprintf(stderr,"Failed to read freemap.sys file\n");
			ret = -1;
			break;
		}
		if ( (verbose&VERBOSE_FREEMAP) )
			dumpFreemap(freemapFileContents, freemapFileHeader.size);
		if ( getFileHeader("rootdir.sys", fd, FSYS_ID_HEADER, indexFileContents + FSYS_INDEX_ROOT * FSYS_MAX_ALTS, &dirFileHeader) && (verbose&VERBOSE_HEADERS))
			displayFileHeader(stdout,&dirFileHeader,1);
		dirFileContents = (uint8_t *)calloc(dirFileHeader.clusters*512,1);
		if ( readFile("rootdir.sys", fd, dirFileContents, dirFileHeader.size, dirFileHeader.pointers[0]) < 0 )
		{
			fprintf(stderr,"Failed to read rootdir.sys file\n");
			ret = -1;
			break;
		}
		if ( (verbose & VERBOSE_DMPROOT) )
			dumpDir(dirFileContents, dirFileHeader.size, fd, indexFileContents );
		if ( (verbose & VERBOSE_ITERATE) )
			iterateDir(0, dirFileContents, dirFileHeader.size, fd, indexFileContents);
		if ( ret < 0 )
			break;
	} while ( 0 );
	if ( fd >= 0 )
		close(fd);
	if ( indexFileContents )
		free(indexFileContents);
	if ( freemapFileContents )
		free(freemapFileContents);
	if ( dirFileContents )
		free(dirFileContents);
	return ret;
}
