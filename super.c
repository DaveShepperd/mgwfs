#include "kmgwfs.h"

static const match_table_t tokens = {
	{ MGWFS_MNT_OPT_ALLOCATION, "allocation=%d" },
	{ MGWFS_MNT_OPT_COPIES, "copies=%d" },
	{ MGWFS_MNT_OPT_VERBOSE, "verbose" },
	{ MGWFS_MNT_OPT_VERBOSE_HOME, "vhome" },
	{ MGWFS_MNT_OPT_VERBOSE_HEADERS, "vheaders" },
	{ MGWFS_MNT_OPT_VERBOSE_DIR, "vdir" },
	{ MGWFS_MNT_OPT_VERBOSE_READ, "vread" },
	{ MGWFS_MNT_OPT_VERBOSE_INODE, "vinode" },
	{ MGWFS_MNT_OPT_VERBOSE_INDEX, "vindex" },
	{ MGWFS_MNT_OPT_VERBOSE_FREE, "vfree" },
	{ MGWFS_MNT_OPT_VERBOSE_UNPACK, "vunpack" },
	{ MGWFS_MNT_OPT_VERBOSE_LOOKUP, "vlookup" },
	{ 0, NULL }
};

static int parse_options(struct super_block *sb, char *options)
{
	MgwfsSuper_t *ourSuper;
	substring_t args[MAX_OPT_ARGS];
	int token, arg;
	char *p;

	ourSuper = MGWFS_SB(sb);
	while ( (p = strsep(&options, ",")) )
	{
		if ( !*p )
			continue;

		args[0].to = args[0].from = NULL;
		token = match_token(p, tokens, args);
		switch (token)
		{
		case MGWFS_MNT_OPT_ALLOCATION:
			arg = 0;
			if ( !args->from || match_int(args, &arg) || arg < 10 || arg > 1000 )
			{
				pr_err("mgwfs_parse_options: match_int failed. allocation '%s' has to be > 10 and < 1000\n", args->from ? args->from : "<null>");
				return -EINVAL;
			}
			ourSuper->defaultAllocation = arg;
			break;
		case MGWFS_MNT_OPT_COPIES:
			arg = 0;
			if ( !args->from || match_int(args, &arg) || arg < 1 || arg > FSYS_MAX_ALTS )
			{
				pr_err("mgwfs_parse_options: match_int failed. copies '%s' has to be > 0 and < %d\n", args->from ? args->from : "<null>", FSYS_MAX_ALTS);
				return -EINVAL;
			}
			ourSuper->defaultCopies = arg;
			break;
		default:
			ourSuper->flags |= token;
			break;
		}
	}
	return 0;
}

uint8_t *mgwfs_getSector(struct super_block *sb, MgwfsBlock_t *buffP, sector_t sector, int *numBytes)
{
	int bufOffset;
	int sectorsPerBlock = sb->s_blocksize/BYTES_PER_SECTOR;
	sector_t block = sector / sectorsPerBlock;
	
	if ( !buffP->bh || buffP->block != block )
	{
		if ( buffP->bh )
			brelse(buffP->bh);
		if ( !(buffP->bh = sb_bread(sb,block)) )
		{
			buffP->block = 0;
			*numBytes = 0;
			return NULL;
		}
		buffP->block = block;
	}
	bufOffset = (sector%sectorsPerBlock)*BYTES_PER_SECTOR;
	buffP->buffOffset = bufOffset;
	if ( numBytes )
		*numBytes = sb->s_blocksize - bufOffset;
	return buffP->bh->b_data + bufOffset;
}

/*
 * There must be a better way to do this, but it looks like all writes
 * of single sectors have to be done via a read-modify-write cycle. I
 * could never get the __bread() function to work so I doubted the __bwrite()
 * function would work either. No matter, nobody is ever going to use this
 * driver so it doesn't have to be fast or efficient.
 */
void mgwfs_putSector(struct super_block *sb, const uint8_t *ptr, sector_t sector)
{
	if ( sb && ptr && sector )
	{
		MgwfsBlock_t buffh;
		uint8_t *buff;
		buffh.bh = NULL;
		buffh.block = 0;
		buff = mgwfs_getSector(sb,&buffh,sector,NULL);
		if ( buff )
		{
			memcpy(buff, ptr, BYTES_PER_SECTOR);
			mark_buffer_dirty(buffh.bh);
			sync_dirty_buffer(buffh.bh);
			brelse(buffh.bh);
		}
	}
}

void mgwfs_putSectors(struct super_block *sb, const uint8_t *ptr, sector_t stSector, int numSectors)
{
	if ( sb && ptr && stSector && numSectors )
	{
		int bytesLeft, bytesToWrite;
		int sectorsPerBlock = sb->s_blocksize/BYTES_PER_SECTOR;
		sector_t block = stSector / sectorsPerBlock;
		MgwfsBlock_t buffh;
		uint8_t *buff;

		bytesToWrite = numSectors*BYTES_PER_SECTOR;		/* Total number of bytes to write */
		while ( bytesToWrite > 0 )
		{
			buffh.bh = NULL;					/* Assume empty */
			buffh.block = 0;
			bytesLeft = 0;
			buff = mgwfs_getSector(sb,&buffh,block,&bytesLeft);
			if ( !buff )
			{
				pr_err("mgwfs_putSectors(): Failed to get sector %lld to write %d sectors.\n", stSector, numSectors);
				break;
			}
			bytesLeft += BYTES_PER_SECTOR;		/* Always at least one sector's worth */
			if ( bytesLeft > bytesToWrite )
				bytesLeft = bytesToWrite;
			memcpy(buff, ptr, bytesLeft);
			mark_buffer_dirty(buffh.bh);
			sync_dirty_buffer(buffh.bh);
			brelse(buffh.bh);
			ptr += bytesLeft;
			bytesToWrite -= bytesLeft;
			block += bytesLeft/BYTES_PER_SECTOR;
		}
	}
}

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

typedef struct bootsect
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
} BootSect_t /* __attribute__((packed)) */;

#if 0
static void displayFileHeader(int alert, FsysHeader *fhp, int retrievalsToo)
{
	FsysRetPtr *rp;
	int ii;
	char txt[256];
	int len;
	
	rp = fhp->pointers[0];
	snprintf(txt,sizeof(txt),
			"%s"
			"mgwfs:    id:        0x%X\n"
			"mgwfs:    size:      %d\n"
			"mgwfs:    clusters:  %d\n"
			"mgwfs:    generation:%d\n"
			"mgwfs:    type:      %d\n"
			"mgwfs:    flags:     0x%04X\n"
			"mgwfs:    ctime:     0x%X\n"
			"mgwfs:    mtime:     0x%X\n"
			,alert ? KERN_ERR : KERN_INFO
			, fhp->id
			, fhp->size
			, fhp->clusters
			, fhp->generation
			, fhp->type
			, fhp->flags
			, fhp->ctime
			, fhp->mtime
		   );
	printk(txt);
	for (ii=0;
		 ii < FSYS_MAX_FHPTRS;
		 ++ii, ++rp )
	{
		if ( !rp->start || !rp->nblocks )
		break;
	}
	rp = fhp->pointers[0];
	len = snprintf(txt,sizeof(txt)-2,"%smgwfs:    pointers (%d of a max of %ld):", alert ? KERN_ERR:KERN_INFO, ii, FSYS_MAX_FHPTRS);
	if ( retrievalsToo )
	{
		for (ii=0; ii < FSYS_MAX_FHPTRS; ++ii, ++rp )
		{
			if ( !rp->start || !rp->nblocks )
				break;
			len += snprintf(txt+len,sizeof(txt)-2-len," 0x%X/%d", rp->start, rp->nblocks);
		}
	}
	else
	{
		len += snprintf(txt+len,sizeof(txt)-2-len," 0x%X/%d%s", rp->start, rp->nblocks, ii > 1 ? " ...":"");
	}
	txt[len++] = '\n';
	txt[len] = 0;
	printk(txt);
}
#endif

int mgwfs_getFileHeader(struct super_block *sb, const char *title, uint32_t headerID, uint32_t fileID, uint32_t lbas[FSYS_MAX_ALTS], FsysHeader *fhp)
{
	FsysHeader *lclHdrs, *lclFhp, *tmpFhp;
	MgwfsSuper_t *ourSuper = (MgwfsSuper_t *)sb->s_fs_info;
	int ii, ret, good = 0, match = 0;
	sector_t sector;
	uint8_t *bufPtr;
//	int displayed=0;
	
	lclHdrs = (FsysHeader *)kzalloc(sizeof(FsysHeader)*FSYS_MAX_ALTS,GFP_KERNEL);
	lclFhp = lclHdrs;
	if ( (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_HEADERS) )
		pr_info("mgwfs_getFileHeader(): Attempting to read file header for fid %d: '%s'. lbas=0x%08X 0x%08X 0x%08X\n", fileID, title, lbas[0], lbas[1], lbas[2]);
	for ( ii = 0; ii < FSYS_MAX_ALTS; ++ii, ++lclFhp )
	{
		sector = lbas[ii] + ourSuper->baseSector;
		if ( (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_HEADERS) )
			pr_info("mgwfs_getFileHeader(): Attempting to read file header for fid %d: '%s' at sector 0x%llX\n", fileID, title, sector);
		bufPtr = mgwfs_getSector(sb,&ourSuper->buffer,sector,NULL);
		if ( !bufPtr )
		{
			pr_err("mgwfs_getFileHeader(): File %d: %s: Sector at 0x%llX cannot be read. mgwfs_getSector() return NULL.\n", fileID, title, sector);
			continue;
		}
		tmpFhp = (FsysHeader *)bufPtr;
		if ( tmpFhp->id != headerID )
		{
			pr_warn("mgwfs_getFileHeader(): File %d: %s: Sector at 0x%llX is not a file header:\n", fileID, title, sector);
//			displayFileHeader(1, tmpFhp, 0);
			continue;
		}
		memcpy(lclFhp, tmpFhp, sizeof(FsysHeader));
		good |= 1 << ii;
		if ( !ii )
		{
			match = 1;
		}
		else
		{
			if ( !memcmp(lclHdrs, lclFhp, sizeof(FsysHeader)) )
				match |= 1 << ii;
			else 
			{
				pr_warn("mgwfs_getFileHeader(): Header %d does not match header 0\nmgwfs(): here is header 0:\n", ii);
//				displayFileHeader(1, lclHdrs, 0);
//				pr_warn("mgwfs_getFileHeader(): And here is header %d:\n", ii);
//				displayFileHeader(1, lclFhp, 0);
//				displayed = 1;
			}
		}
	}
	ret = 1;
	if ( good )
	{
		lclFhp = lclHdrs;
		if ( !(good & 1) )
		{
			++lclFhp;
			if ( !(good & 2) )
				++lclFhp;
		}
		memcpy(fhp, lclFhp, sizeof(FsysHeader));
	}
	else
	{
		memset(fhp, 0, sizeof(FsysHeader));
		ret = 0;
	}
	kfree(lclHdrs);
//	if ( !displayed && (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_HEADERS) )
//		displayFileHeader(0, tmpFhp, 0);
	return ret;
}

#if 0
int mgwfs_extendFile(struct super_block *sb, int sectors, FsysHeader *fhp)
{
	MgwfsSuper_t *ourSuper = MGWFS_SB(sb);
	if ( !sectors )
		sectors = ourSuper->defaultAllocation;
	if ( fhp->size+BYTES_PER_SECTOR-1 > sectors*BYTES_PER_SECTOR )
		sectors = (fhp->size+BYTES_PER_SECTOR-1)/BYTES_PER_SECTOR;
}

static uint32_t getNewHeaderSector(struct super_block *sb, int idx)
{
	MgwfsSuper_t *ourSuper = MGWFS_SB(sb);
	MgwfsFoundFreeMap_t freeM;
	FsysRetPtr *retp;
	int ii;
	
	memset(&freeM,0,sizeof(freeM));
	retp = ourSuper->indexSysHdr.pointers[idx];
}

int mgwfs_allocFileHeader(struct super_block *sb, uint32_t *fid, FsysHeader *fhp)
{
	MgwfsSuper_t *ourSuper = MGWFS_SB(sb);
	uint32_t *idp, *lastEntry, *maxEntry;
	
	idp = ourSuper->indexSys;
	lastEntry = idp+(ourSuper->indexSysHdr.size+sizeof(uint32_t)-1)/sizeof(uint32_t);
	maxEntry = idp + (ourSuper->indexSysHdr.clusters*BYTES_PER_SECTOR)/sizeof(uint32_t);
	idp += 4;		/* Skip checking the first 4 items */
	for (; idp < lastEntry; ++idp )
	{
		/* Look through index.sys for an unused entry */
		if ( (*idp & FSYS_EMPTYLBA_BIT) )
			break;
	}
	if ( idp == maxEntry )
	{
		*fid = 0;	
		pr_info("mgwfs(): No room for anymore fileheaders. All %ld used\n", idp-ourSuper->indexSys );
		return ENOSPC;
	}
	if ( idp >= lastEntry )
	{
		ourSuper->indexSysHdr.size += sizeof(uint32_t);
		ourSuper->indexFHDirty = true;
	}
	*fid = idp-ourSuper->indexSys;
	{
	}
}
#endif

int mgwfs_readFile(struct super_block *sb, const char *title, uint8_t *dst, int bytes, FsysRetPtr *retPtr, int squawk)
{
	MgwfsSuper_t *ourSuper = sb->s_fs_info;
	sector_t sector;
	int sectorIdx;
	int ptrIdx, retSize;
	int bytesInBuffer;
	uint8_t *bufPointer;
	ssize_t limit;
	
	if ( (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_READ) )
	{
		int num;
		for (num=0; num < FSYS_MAX_FHPTRS; ++num)
		{
			if ( !retPtr[num].nblocks || !retPtr[0].start )
				break;
		}
		pr_info("mgwfs_readfile(): Attempting to read %d bytes for %s. starting sector=0x%X, nSectors=%d, numRetPtrs=%d\n",
				bytes, title, retPtr->start, retPtr->nblocks, num);
	}
	retSize = 0;
	sectorIdx = 0;
	ptrIdx = 0;
	while ( retSize < bytes && ptrIdx < FSYS_MAX_FHPTRS )
	{
		if ( !retPtr->start || !retPtr->nblocks  )
		{
			pr_warn("mgwfs(): Empty retrieval pointer at retIdx %d while reading '%s'\n", ptrIdx, title);
			return retSize ? retSize : -1;
		}
		sector = retPtr->start + sectorIdx;
		bufPointer = mgwfs_getSector(sb,&ourSuper->buffer,sector,&bytesInBuffer);
		BUG_ON(!bufPointer);
		limit = bytesInBuffer;
		if ( sectorIdx + limit/BYTES_PER_SECTOR > retPtr->nblocks )
			limit = (retPtr->nblocks-sectorIdx)*BYTES_PER_SECTOR;
		if ( limit > bytes - retSize )
			limit = bytes - retSize;
		memcpy(dst, bufPointer, limit);
		if ( squawk && (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_READ) )
		{
			uint32_t *ip = (uint32_t *)bufPointer;
			pr_info("mgwfs_readfile(): mgwfs_getSector(,0x%llX,) returned %p and bytesInBuffer %d. retSize=%d, limit=%ld\n",
					sector, bufPointer, bytesInBuffer, retSize, limit);
			pr_info("mgwfs_readfile(): buffer [0]=0x%08X [1]=0x%08X [2]=0x%08X\n", ip[0], ip[1], ip[2]);
		}
		dst += limit;
		retSize += limit;
		sectorIdx += (limit+BYTES_PER_SECTOR-1)/BYTES_PER_SECTOR;
		if ( sectorIdx >= retPtr->nblocks )
		{
			/* ran off the end of a retrieval pointer set so advance to next one */
			sectorIdx = 0;
			++retPtr;
			++ptrIdx;
			if ( (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_READ) )
				pr_warn("mgwfs_readfile(): advanced to retrieval ptr %d: 0x%X, nSectors=%d\n",
						ptrIdx, retPtr->start, retPtr->nblocks);
			if ( ptrIdx >= FSYS_MAX_FHPTRS )
			{
				pr_err("mgwfs_readfile(): Too many retrieval pointers\n");
				break;
			}
		}
	}
	return retSize;
}

static void displayHomeBlock(sector_t sector, const FsysHomeBlock *homeBlkp, uint32_t cksum)
{
	pr_warn(
		   "mgwfs:    sector:    0x%08llX\n"
		   "mgwfs:    id:        0x%08X\n"
		   "mgwfs:    hb_minor:  %d\n"
		   "mgwfs:    hb_major:  %d\n"
		   "mgwfs:    hb_size:   %d\n"
		   "mgwfs:    fh_minor:  %d\n"
		   "mgwfs:    fh_major:  %d\n"
		   "mgwfs:    fh_size:   %d\n"
		   "mgwfs:    efh_minor: %d\n"
		   "mgwfs:    efh_major: %d\n"
		   "mgwfs:    efh_size:  %d\n"
		   "mgwfs:    efh_ptrs:  %d\n"
		   "mgwfs:    rp_minor:  %d\n"
		   "mgwfs:    rp_major:  %d\n"
		   "mgwfs:    rp_size:   %d\n"
		   "mgwfs:    cluster:   %d\n"
		   "mgwfs:    maxalts:   %d\n"
		   ,sector
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
	pr_warn(   "mgwfs:    def_extend:%d\n"
			   "mgwfs:    ctime:     %d\n"
			   "mgwfs:    mtime:     %d\n"
			   "mgwfs:    atime:     %d\n"
			   "mgwfs:    btime:     %d\n"
			   "mgwfs:    chksum:    0x%X (computed 0x%X)\n"
			   "mgwfs:    features:  0x%08X\n"
			   "mgwfs:    options:   0x%08X\n"
			   "mgwfs:    index[]:   0x%08X,0x%08X,0x%08X\n"
			   "mgwfs:    boot[]:    0x%08X,0x%08X,0x%08X\n"
			   "mgwfs:    max_lba:   0x%08X\n"
			   "mgwfs:    upd_flag:  %d\n"
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
	pr_warn("mgwfs:    boot1[]:   0x%08X, 0x%08X, 0x%08X\n"
		   "mgwfs:    boot2[]:   0x%08X, 0x%08X, 0x%08X\n"
		   "mgwfs:    boot3[]:   0x%08X, 0x%08X, 0x%08X\n"
		   "mgwfs:    journal[]: 0x%08X, 0x%08X, 0x%08X\n"
		   ,homeBlkp->boot1[0], homeBlkp->boot1[1], homeBlkp->boot1[2]
		   ,homeBlkp->boot2[0], homeBlkp->boot2[1], homeBlkp->boot2[2]
		   ,homeBlkp->boot3[0], homeBlkp->boot3[1], homeBlkp->boot3[2]
		   ,homeBlkp->journal[0], homeBlkp->journal[1], homeBlkp->journal[2]
		   );
}

static int getOurSuper(struct super_block *sb)
{
	FsysHomeBlock lclHomes[FSYS_MAX_ALTS], *lclHome = lclHomes;
	int ii, good = 0, match = 0;
	sector_t sector;
	int jj;
	uint32_t options;
	uint32_t *csp, cksum;
	BootSect_t *bootSect;
	uint32_t baseSector = 0;
	int sizeInSectors = 0;
	int maxHb;
	uint32_t homeLbas[FSYS_MAX_ALTS], *indexSys;
	MgwfsSuper_t *ourSuper;
	uint8_t *bufPointer;
	int flags;
	
	ourSuper = (MgwfsSuper_t *)sb->s_fs_info;
	flags= ourSuper->flags;
	memset(lclHomes, 0, sizeof(lclHomes));
	maxHb = FSYS_HB_RANGE;
	bufPointer = mgwfs_getSector(sb,&ourSuper->buffer,0,NULL);
	BUG_ON(!bufPointer);
	if ( ourSuper->buffer.bh->b_size != sb->s_blocksize )
	{
		pr_err("mgwfs_getOurSuper(): Fatal error. s_blocksize=%ld, bh->b_size=%ld\n",
			   sb->s_blocksize, ourSuper->buffer.bh->b_size);
		return -EINVAL;
	}
	bootSect = (BootSect_t *)bufPointer;
	for ( ii = 0; ii < 4; ++ii )
	{
		if ( bootSect->parts[ii].status == 0x80 && bootSect->parts[ii].type == 0x8f )
		{
			baseSector = bootSect->parts[ii].abs_sect;
			sizeInSectors = bootSect->parts[ii].num_sects;
			maxHb = sizeInSectors > FSYS_HB_RANGE ? FSYS_HB_RANGE : sizeInSectors;
			if ( (flags & MGWFS_MNT_OPT_VERBOSE_HOME) )
				pr_info("mgwfs_getHome(): Found an agc fsys partition in partition %d. baseSector=0x%X, numSectors=0x%X, maxHb=0x%X\n",
						ii, baseSector, sizeInSectors, maxHb);
			break;
		}
	}
	if ( (flags & MGWFS_MNT_OPT_VERBOSE_HOME) && ii >= 4 )
		pr_info("mgwfs_getHome(): No parition table found\n");
	bootSect = NULL;
	for ( ii = 0; ii < FSYS_MAX_ALTS; ++ii )
		homeLbas[ii] = FSYS_HB_ALG(ii, maxHb);
	for ( ii = 0; ii < FSYS_MAX_ALTS; ++ii, ++lclHome )
	{
		sector = homeLbas[ii] + baseSector;
		if ( (flags & MGWFS_MNT_OPT_VERBOSE_HOME) )
			pr_info("mgwfs_getHomeBlock(): Attempting to read home block at sector 0x%llX\n", sector);
		bufPointer = mgwfs_getSector(sb,&ourSuper->buffer,sector,NULL);
		BUG_ON(!bufPointer);
		memcpy(lclHome, bufPointer, sizeof(FsysHomeBlock));
		cksum = 0;
		csp = (uint32_t *)lclHome;
		for ( jj = 0; jj < sizeof(FsysHomeBlock) / sizeof(uint32_t); ++jj )
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
			good |= 1 << ii;
		}
		else
		{
			pr_warn("mgwfs_getHome(): Home block %d is not what is expected:\n", ii);
			displayHomeBlock(sector, lclHome, cksum);
			continue;
		}
		if ( !ii )
		{
			match = 1;
		}
		else
		{
			if ( !memcmp(lclHomes + 0, lclHome, sizeof(FsysHomeBlock)) )
				match |= 1 << ii;
			else
				pr_warn("mgwfs_getHome(): Header %d does not match header 0\n", ii);
		}
	}
	if ( !good )
	{
		return -EINVAL;
	}
	lclHome = lclHomes;
	if ( !(good & 1) )
	{
		++lclHome;
		if ( !(good & 2) )
			++lclHome;
	}
	ourSuper->flags = flags;
	memcpy(&ourSuper->homeBlk, lclHome, sizeof(ourSuper->homeBlk));  /* Keep a copy of a good home block */
	ourSuper->baseSector = baseSector;
	if ( !mgwfs_getFileHeader(sb, "index.sys", FSYS_ID_INDEX, FSYS_INDEX_INDEX, ourSuper->homeBlk.index, &ourSuper->indexSysHdr) )
	{
		return -EINVAL;
	}
	ourSuper->indexSys = (uint32_t *)kzalloc(ourSuper->indexSysHdr.clusters*BYTES_PER_SECTOR, GFP_KERNEL);
	if ( !ourSuper->indexSys )
	{
		return -ENOMEM;
	}
	for (ii=0; ii < FSYS_MAX_ALTS; ++ii)
	{
		if ( mgwfs_readFile(sb, "index.sys", (uint8_t *)ourSuper->indexSys, ourSuper->indexSysHdr.size, ourSuper->indexSysHdr.pointers[ii],1) == ourSuper->indexSysHdr.size )
			break;
	}
	if ( ii >= FSYS_MAX_ALTS )
	{
		pr_err("mgwfs(): Failed to read index.sys\n");
		return -EINVAL;
	}
	ourSuper->indexAvailable = (ourSuper->indexSysHdr.clusters*BYTES_PER_SECTOR)/(sizeof(uint32_t)*FSYS_MAX_ALTS);
	indexSys = ourSuper->indexSys;
	for (ii=0; ii < ourSuper->indexAvailable; ++ii, indexSys += FSYS_MAX_ALTS)
	{
		if ( !*indexSys )
			break;
		if ( !(*indexSys&FSYS_EMPTYLBA_BIT) )
			++ourSuper->indexUsed;
	}
	if ( (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_INDEX) )
	{
		int ii=0;
		char txt[256];
		int len;
		uint32_t bytes = ourSuper->indexSysHdr.size;
		int entries = (bytes+sizeof(uint32_t)-1)/sizeof(uint32_t);
		indexSys = ourSuper->indexSys;
		static const char *Titles[] = 
		{
			"index.sys",
			"freemap.sys",
			"rootdir.sys",
			"journal.sys"
		};
		
		pr_info("mgwfs(): Contents of index.sys:\n");
		while ( indexSys < ourSuper->indexSys+entries )
		{
			len = snprintf(txt,sizeof(txt),"mgwfs(): %5d: 0x%08X 0x%08X 0x%08X", ii, indexSys[0], indexSys[1], indexSys[2]);
			if ( ii < 4 )
				len += snprintf(txt+len,sizeof(txt)-len," (%s)", Titles[ii]);
			pr_info("%s\n",txt);
			++ii;
			indexSys += 3;
		}
	}
	if ( mgwfs_getFileHeader(sb, "freemap.sys", FSYS_ID_HEADER, FSYS_INDEX_FREE, ourSuper->indexSys + FSYS_INDEX_FREE*FSYS_MAX_ALTS, &ourSuper->freeMapHdr) )
	{
		ourSuper->freeMap = (FsysRetPtr *)kzalloc(ourSuper->freeMapHdr.clusters * BYTES_PER_SECTOR, GFP_KERNEL);
		if ( ourSuper->freeMap )
		{
			for (ii=0; ii < FSYS_MAX_ALTS; ++ii)
			{
				if ( mgwfs_readFile(sb, "freemap.sys", (uint8_t *)ourSuper->freeMap, ourSuper->freeMapHdr.size, ourSuper->freeMapHdr.pointers[ii],1) == ourSuper->freeMapHdr.size )
					break;
			}
			if ( ii >= FSYS_MAX_ALTS )
			{
				pr_err("mgwfs(): Failed to read freemap.sys. Ignored.\n");
				kfree(ourSuper->freeMap);
				ourSuper->freeMap = NULL;
			}
			else
			{
				int maxEntries = (ourSuper->freeMapHdr.clusters*BYTES_PER_SECTOR)/sizeof(FsysRetPtr);
				FsysRetPtr *rp = ourSuper->freeMap, *rpMax = ourSuper->freeMap + maxEntries - 1;
				while ( rp < rpMax && rp->nblocks && rp->start )
					++rp;
				ourSuper->freeMapEntriesUsed = rp-ourSuper->freeMap;
				ourSuper->freeMapEntriesAvail = rpMax-ourSuper->freeMap;
			}
		}
		else
			pr_err("mgwfs(): Failed to kzalloc(%d) bytes for freemap.sys. Ignored.\n", ourSuper->freeMapHdr.clusters * BYTES_PER_SECTOR);
	}
	return good;
}

static int fill_super(struct super_block *sb, void *data, int silent)
{
	MgwfsSuper_t *ourSuper;
	struct inode *root_inode;
	MgwfsInode_t *root_mgwfs_inode;
	int ret, flags=0;

	ourSuper = (MgwfsSuper_t *)kzalloc(sizeof(MgwfsSuper_t), GFP_KERNEL); /* Allocate our super block memory*/
	if ( !ourSuper )
		return -ENOMEM;
	sb->s_fs_info = ourSuper;
	sb->s_blocksize = PAGE_SIZE;			/* system normal */
	sb->s_blocksize_bits = PAGE_SHIFT;		/* normal system shift */
	sb->s_maxbytes = 0x00FFFFFF/4;			/* Not really an upper limit, but set this just for grins and giggles */
	sb->s_time_gran = 1;					/* mgwfs timestamps are in 1 second intervals, UTC */
	ourSuper->defaultAllocation = 128;		/* Assume a default allocation */
	ourSuper->defaultCopies = 1;			/* Assume a default number of copies */
	ourSuper->hisSb = sb;					/* Cross link our super blocks */
	ret = parse_options(sb, data); 
	flags = ourSuper->flags;
	if ( ret )
	{
		pr_err("mgwfs_fill_super: Failed to parse options, error code: %d\n",
			   ret);
	}
	if ( flags )
	{
		pr_info("mgwfs_fill_super(): s_blocksize=%lu, is_ro=%s, ourFlags=0x%X, copies=%d, allocation=%d\n"
				,sb->s_blocksize
				,(sb->s_flags&SB_RDONLY)?"Yes":"No"
				,flags
				,ourSuper->defaultCopies
				,ourSuper->defaultAllocation
				);
	}
	if ( (ret=getOurSuper(sb)) <= 0 )
	{
		if ( !ret )
			ret = -EINVAL;
		return ret;
	}
	ourSuper = (MgwfsSuper_t *)sb->s_fs_info;
	if ( ourSuper->homeBlk.cluster != 1 )
	{
		printk(KERN_ERR
			   "mgwfs_fill_super(): seems to be formatted with mismatching cluster size: %u. s/b %u\n",
			   ourSuper->homeBlk.cluster, 1);
		return -EINVAL;
	}
	sb->s_magic = ourSuper->homeBlk.id;
	sb->s_op = &mgwfs_sb_ops;
	if ( !sb_rdonly(sb) )
	{
		pr_warn("mgwfs_fill_super(): Not mounted ro, setting ro flag\n");
		sb->s_flags |= SB_RDONLY;
	}
	root_mgwfs_inode = mgwfs_get_mgwfs_inode(sb, FSYS_INDEX_ROOT, 0, "rootdir.sys");
	if ( !root_mgwfs_inode )
	{
		pr_err("mgwfs_fill_super(): Failed to get our inode for root dir\n");
		return -EINVAL;
	}
	root_inode = new_inode(sb);
	if ( !root_inode || !root_mgwfs_inode )
		return -ENOMEM;
	mgwfs_fill_inode(sb, root_inode, root_mgwfs_inode, "rootdir.sys");
	inode_init_owner(&nop_mnt_idmap, root_inode, NULL, S_IFDIR|0555);
	sb->s_root = d_make_root(root_inode);
	if ( !sb->s_root )
	{
		return -ENOMEM;
	}
	pr_info("mgwfs_get_super(): root parent dir=%p, inode=%p\n", sb->s_root, root_inode);
//	root_mgwfs_inode->parentDentry = sb->s_root;
#if 0
	ret = mgwfs_unpackDir("mgwfs_fill_super():", sb->s_root, ourSuper, root_inode);
	if ( (ourSuper->flags & MGWFS_MNT_OPT_VERBOSE_UNPACK) )
	{
		MgwfsInode_t *ptr;
		int dirEnt=0;
		
		ptr = root_mgwfs_inode->children;
		pr_info("mgwfs_fill_super(): nextInode=%p, children=%p, numDirEntries=%d\n",
				root_mgwfs_inode->nextInode,
				ptr,
				root_mgwfs_inode->numDirEntries
				);
		while ( ptr )
		{
			pr_info("mgwfs_fill_super(): %3d 0x%06X %s%s\n",
					dirEnt,
					ptr->inode_no,
					ptr->fileName,
					S_ISDIR(ptr->mode)?"/":""
					);
			ptr = ptr->nextInode;
			++dirEnt;
		}
	}
#else
	ret = 0;
#endif
	return ret;
}

static void local_statfs(struct super_block *sb, MgwfsSuper_t *sbi, struct kstatfs *statp)
{
	int numPtrs, totFreeSectors=0, sectorsPerBlock;
	FsysRetPtr *retPtr;
	loff_t ffreeFiles;
	
	statp->f_type = sbi->homeBlk.id;
	statp->f_bsize = sb->s_blocksize;
	sectorsPerBlock = sb->s_blocksize/BYTES_PER_SECTOR;
	statp->f_blocks = sbi->homeBlk.max_lba/sectorsPerBlock;
	if ( (retPtr = sbi->freeMap) )
	{
		numPtrs = sbi->freeMapHdr.size/sizeof(FsysRetPtr);
		for (;numPtrs > 0; --numPtrs, ++retPtr)
		{
			if ( !retPtr->nblocks )
				break;
			totFreeSectors += retPtr->nblocks;
		}
	}
	statp->f_bfree = totFreeSectors/sectorsPerBlock;
	statp->f_bavail = statp->f_bfree;
	statp->f_files = sbi->indexSysHdr.size/(sizeof(uint32_t)*FSYS_MAX_ALTS);
	ffreeFiles = sbi->indexSysHdr.clusters;
	ffreeFiles = (ffreeFiles*BYTES_PER_SECTOR)/(sizeof(uint32_t)*FSYS_MAX_ALTS);
	statp->f_ffree = ffreeFiles - statp->f_files;
	statp->f_flags = sb->s_flags;
	statp->f_frsize = sb->s_blocksize;
	statp->f_namelen = MGWFS_FILENAME_MAXLEN;
}

struct dentry* mgwfs_mount(struct file_system_type *fs_type,
						   int flags, const char *dev_name,
						   void *data)
{
	struct dentry *ret;
	ret = mount_bdev(fs_type, flags, dev_name, data, fill_super);
//	pr_info("mgwfs_mount(): mount_bdev() (fill_super()) returned %p\n", ret);
	if ( IS_ERR_OR_NULL(ret) )
	{
		pr_err("Error mounting mgwfs on %s. ret=%p\n", dev_name, ret);
	}
	else
	{
		struct super_block *sb;
		MgwfsSuper_t *ourSuper;
		sb = ret->d_sb;
		ourSuper = sb ? MGWFS_SB(sb) : NULL;
		if ( ourSuper && ourSuper->flags )
		{
			struct kstatfs stat;
			local_statfs(sb,ourSuper,&stat);
			pr_info("mgwfs is succesfully mounted on: %s, ret=%p, sb=%p, ourSuper=%p\n",
					dev_name, ret, sb, ourSuper);
			pr_info("mgwfs blksz %ld, blocks %lld, bfree %lld, bavail %lld, files %lld, freeFiles %lld\n",
					stat.f_bsize, stat.f_blocks, stat.f_bfree, stat.f_bavail, stat.f_files, stat.f_ffree );
		}
	}

	return ret;
}

void mgwfs_kill_superblock(struct super_block *sb)
{
	MgwfsSuper_t *ourSuper;
	if ( (ourSuper = sb->s_fs_info) )
	{
		if ( ourSuper->buffer.bh )
			brelse(ourSuper->buffer.bh);
		if ( ourSuper->freeMap )
			kfree(ourSuper->freeMap);
		if ( ourSuper->indexSys )
			kfree(ourSuper->indexSys);
	}
	kill_block_super(sb);
	if ( ourSuper )
		kfree(ourSuper);
	pr_info("mgwfs_kill_superblock(): Unmount succesful.\n");
}

#if 0
void mgwfs_put_super(struct super_block *sb)
{
	return;
}
#endif

#if 0
void mgwfs_save_sb(struct super_block *sb)
{
	struct buffer_head *bh;
	struct mgwfs_superblock *mgwfs_sb = MGWFS_SB(sb);

	bh = sb_bread(sb, MGWFS_SUPERBLOCK_BLOCK_NO);
	BUG_ON(!bh);

	bh->b_data = (char *)mgwfs_sb;
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);
}
#endif

int mgwfs_statfs(struct dentry *dirp, struct kstatfs *statp)
{
	struct super_block *sb = dirp->d_sb;
	MgwfsSuper_t *sbi = MGWFS_SB(sb);
	
	local_statfs(sb,sbi,statp);
	return 0;
}

