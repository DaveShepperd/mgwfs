/*
  mgwfs: Atari/MidwayGamesWest filesystem using libfuse: Filesystem in Userspace

  Copyright (C) 2025  Dave Shepperd <mgwfs@dshepperd.com>

  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.

 Build with enclosed Makefile

*/

#include "mgwfs.h"
#include "version.h"

static void helpEm(FILE *ofp, const char *progname)
{
	fprintf(ofp, "Usage: %s [options] <mountpoint>\n", progname);
	fprintf(ofp, "Filesystem specific options:\n"
		   "--allocation=n  Specify the default allocation in sectors (default=100)\n"
		   "--copies=n      Specify the default number of copies of each file to write (default=1)\n"
		   "--log=<path>    Specify a path to a logfile (default=stdout)\n"
		   "--image=<path>  Specify a path to filesystem file (required)\n"
		   "--readwrite     Specify to allow writing (default is readonly)\n"
		   "--rw            Specify to allow writing (default is readonly)\n"
		   "--testpath=<path> Specify a test path into filesystem file (forces a -q)\n"
		   "--verbose=n 'n' is bit mask of verbose modes:\n"
		   "            May be expressed with normal C syntax [i.e. prefix 0x or 0b for hex or binary]:\n"
		   );
	fprintf(ofp, "    0x%05X = display some small details\n", VERBOSE_MINIMUM);
	fprintf(ofp, "    0x%05X = display home block\n", VERBOSE_HOME);
	fprintf(ofp, "    0x%05X = display file headers\n", VERBOSE_HEADERS);
	fprintf(ofp, "    0x%05X = display all retrieval pointers in file headers\n", VERBOSE_RETPTRS);
	fprintf(ofp, "    0x%05X = display attempts at file reads\n", VERBOSE_READ);
	fprintf(ofp, "    0x%05X = display contents of index.sys file\n", VERBOSE_INDEX);
	fprintf(ofp, "    0x%05X = display freemap primitive actions\n", VERBOSE_FREE);
	fprintf(ofp, "    0x%05X = display contents of freemap.sys file\n", VERBOSE_FREEMAP);
	fprintf(ofp, "    0x%05X = display and verify contents of freemap.sys file\n", VERBOSE_VERIFY_FREEMAP);
	fprintf(ofp, "    0x%05X = display contents of rootdir.sys file\n", VERBOSE_DMPROOT);
	fprintf(ofp, "    0x%05X = display details during unpack()\n", VERBOSE_UNPACK);
	fprintf(ofp, "    0x%05X = display directory searches \n", VERBOSE_LOOKUP);
	fprintf(ofp, "    0x%05X = display details during directory searches\n", VERBOSE_LOOKUP_ALL);
	fprintf(ofp, "    0x%05X = display directory tree\n", VERBOSE_ITERATE);
	fprintf(ofp, "    0x%05X = display anything FUSE related\n", VERBOSE_FUSE);
	fprintf(ofp, "    0x%05X = display FUSE function calls\n", VERBOSE_FUSE_CMD);
	fprintf(ofp, "    0x%05X = display anything related to file writes\n", VERBOSE_WRITES);
#if !NO_MUTEXES
	fprintf(ofp, "    0x%05X = display details of locks/unlocks\n", VERBOSE_LOCKS);
#endif
	fprintf(ofp,
			"-v           Sets verbose flag to a value of 0x001\n"
			"-q or --quit Quit before starting fuse stuff (i.e. just read home blocks, don't mount)\n"
			"-h or --help Print this message\n"
			);
}

#define OPTION(t, p )                           \
    { t, offsetof(Options_t, p), 1 }
	
static const char VerboseStr[] = "--verbose";

static const struct fuse_opt option_spec[] =
{
	OPTION( "--allocation=%lu", allocation ),
	OPTION( "--copies=%lu", copies ),
	OPTION( "--image=%s", image ),
	OPTION( "--testpath=%s", testPath ),
	OPTION( "--log=%s", logFile ),
	{ VerboseStr, -1, FUSE_OPT_KEY_OPT},
	OPTION("-v", verbose ),
	OPTION("-h", show_help ),
	OPTION("--help", show_help ),
	OPTION("-q", quit ),
	OPTION("--quit", quit ),
	OPTION("--version", show_version ),
	OPTION("--readwrite", read_write ),
	OPTION("--rw", read_write ),
	OPTION("-w", read_write ),
	FUSE_OPT_END
};

static int procOption(void *data, const char *arg, int key, struct fuse_args *outargs)
{
	int ret=1;
	
	int sLen=sizeof(VerboseStr)-1;
	
//	printf("ProcOption('%s') checking for match\n", arg);
	if ( !strncmp(arg,VerboseStr,sLen) )
	{
		char *endp=NULL;
		if ( arg[sLen] == '=' )
			++sLen;
		options.verbose = strtoul(arg + sLen, &endp, 0);
		if ( !endp || *endp )
		{
			printf("Invalid argument on %s\n", arg);
			ret = -1;
		}
		ret = 0;
	}
//	printf("ProcOption('%s') returned %d\n", arg, ret);
	return ret;   // -1 on error, 0 if to toss, 1 if to keep
}

static uint32_t getLe(uint8_t *ptr)
{
	uint32_t ans;
	ans = (ptr[3]<<24)|(ptr[2]<<16)|(ptr[1]<<8)|ptr[0];
	return ans;
}

int main(int argc, char *argv[])
{
	int ii;
	int ret;
	uint32_t ckSum;
	MgwfsInode_t *inode, **inodePtr;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	/* Parse options */
	if (fuse_opt_parse(&args, &options, option_spec, procOption) == -1)
		return 1;

	/* When --help is specified, first print our own file-system
	   specific help text, then signal fuse_main to show
	   additional help (by adding `--help` to the options again)
	   without usage: line (by setting argv[0] to the empty
	   string) */
	if ( !options.show_help && !options.show_version && (!options.image || options.image[0] == 0) )
	{
		fprintf(stderr, "No image name provided. Requires a --image=<path> option\n");
		helpEm(stderr,argv[0]);
		return 1;
	}
	if ( options.show_help )
	{
		helpEm(stderr,argv[0]);
		assert(fuse_opt_add_arg(&args, "--help") == 0);
		args.argv[0][0] = '\0';
	}
	else if ( options.show_version )
	{
		printf("%s version %s\n", argv[0], VERSION);
		assert(fuse_opt_add_arg(&args, "--version") == 0);
		args.argv[0][0] = '\0';
	}
	if ( options.testPath )
		options.quit = 1;
	if ( options.logFile )
	{
		ourSuper.logFile = fopen(options.logFile,"w");
		if ( !ourSuper.logFile )
		{
			fprintf(stderr,"Error opening log file '%s': %s\n", options.logFile, strerror(errno));
			return 1;
		}
		ourSuper.errFile = ourSuper.logFile;
	}
	else
	{
		ourSuper.logFile = stdout;
		ourSuper.errFile = stderr;
	}
	if ( options.copies > FSYS_MAX_ALTS )
	{
		fprintf(stderr,"--copies '%ld' can only be 1 through %d\n", options.copies, FSYS_MAX_ALTS);
		return 1;
	}
	if ( !options.copies )
		options.copies = 1;
	if ( options.verbose )
	{
		printf("%s version %s\n", argv[0], VERSION);
		fprintf(ourSuper.logFile, "Allocation=%ld, copies=%ld, verbose=0x%lX, imageName=%s, quit=%ld, readwrite=%ld, logFile='%s'\n",
			   options.allocation, options.copies, options.verbose, options.image, options.quit, options.read_write, options.logFile);
	}
	ourSuper.verbose = options.verbose;
	ourSuper.defaultAllocation = options.allocation;
	ourSuper.defaultCopies = options.copies;
	ourSuper.imageName = options.image;
	if ( !options.show_help && !options.show_version )
	{
		if ( ourSuper.verbose )
		{
			fprintf(ourSuper.logFile, "__WORDSIZE=%d\n", __WORDSIZE);
			fprintf(ourSuper.logFile, "sizeof char=%ld, int=%ld, short=%ld, long=%ld, *=%ld, long long=%ld\n",
							sizeof(char), sizeof(int), sizeof(short), sizeof(long), sizeof(char *), sizeof(long long));
			fprintf(ourSuper.logFile, "sizeof unit8_t=%ld, uint16_t=%ld, uint32_t=%ld, uint64_t=%ld\n",
							sizeof(uint8_t), sizeof(uint16_t), sizeof(uint32_t), sizeof(uint64_t));
			fprintf(ourSuper.logFile, "llval=%llX, lval=%lX, int=%X\n", (long long)0x1234, (long)0x4567, (int)0x89AB);
			fprintf(ourSuper.logFile, "FSYS_FEATURES=0x%08X, FSYS_OPTIONS=0x%08X, FSYS_MAX_ALTS=%d, FSYS_MAX_FHPTRS=%ld\n", FSYS_FEATURES, FSYS_OPTIONS, FSYS_MAX_ALTS, FSYS_MAX_FHPTRS);
			fprintf(ourSuper.logFile, "Partition offset is 0x%04lX(%ld). s/b 0x1BE(446). Sizeof Part is %ld, s/b 16\n",
				   offsetof(BootSector_t,parts),offsetof(BootSector_t,parts), sizeof(Partition));
		}
		do
		{
			int fLim;
			struct stat st;
			off64_t maxHb;
			off64_t sizeInSectors;
	
			if ( FSYS_MAX_ALTS != 3 )
			{
				fprintf(ourSuper.errFile, "FSYS_MAX_ALTS=%d. Application has to be built with it set to 3.\n", FSYS_MAX_ALTS);
				ret = -1;
				break;
			}
			ret = stat(ourSuper.imageName, &st);
			if ( ret < 0 )
			{
				fprintf(ourSuper.errFile,"Unable to stat '%s': %s\n", ourSuper.imageName, strerror(errno));
				break;
			}
			ourSuper.fd = open(ourSuper.imageName, options.read_write ? O_RDWR : O_RDONLY);
			if ( ourSuper.fd < 0 )
			{
				fprintf(ourSuper.errFile, "Error opening the '%s': %s\n", ourSuper.imageName, strerror(errno));
				ret = -1;
				break;
			}
	
			sizeInSectors = st.st_size/512;
			maxHb = sizeInSectors > FSYS_HB_RANGE ? FSYS_HB_RANGE:sizeInSectors;
			ourSuper.maxHb = maxHb;
			if ( (ourSuper.verbose&VERBOSE_HOME) )
			{
				fprintf(ourSuper.logFile, "File size 0x%lX, maxSector=0x%lX, maxHb=0x%lX\n", st.st_size, sizeInSectors, maxHb);
				fprintf(ourSuper.logFile, "Attempting to read a partition table that might be present\n");
			}
			if ( (sizeof(bootSect) != read(ourSuper.fd,&bootSect,sizeof(bootSect))) )
			{
				fprintf(ourSuper.errFile, "Failed to read boot sector: %s\n", strerror(errno));
				ret = -2;
				break;
			}
			for (ii=0; ii < 4; ++ii)
			{
				if ( bootSect.parts[ii].status == 0x80 && bootSect.parts[ii].type == 0x8f )
				{
					ourSuper.baseSector = getLe(bootSect.parts[ii].abs_sect);
					sizeInSectors = getLe(bootSect.parts[ii].num_sects);
					maxHb = sizeInSectors > FSYS_HB_RANGE ? FSYS_HB_RANGE:sizeInSectors;
					if ( (ourSuper.verbose&VERBOSE_HOME) )
						fprintf(ourSuper.logFile, "Found an agc fsys partition in partition %d. baseSector=0x%X, numSectors=0x%lX, maxHb=0x%lX\n", ii, ourSuper.baseSector, sizeInSectors, maxHb);
					break;
				}
			}
			if ( (ourSuper.verbose&VERBOSE_HOME) && ii >= 4 )
				fprintf(ourSuper.logFile, "No parition table found\n");
			for (ii=0; ii < FSYS_MAX_ALTS; ++ii)
				ourSuper.homeLbas[ii] = FSYS_HB_ALG(ii, maxHb);
			if ( !getHomeBlock(&ourSuper,maxHb,sizeInSectors,&ckSum) )
			{
				ret = -1;
				break;
			}
			if ( !ourSuper.homeBlk.max_lba )
				ourSuper.homeBlk.max_lba = sizeInSectors;
			ourSuper.freeMap.sectorsFree = ourSuper.homeBlk.max_lba - 1 - FSYS_MAX_ALTS;
			ourSuper.freeMap.sectorsUsed = FSYS_MAX_ALTS;
			if ( (ourSuper.verbose&VERBOSE_HOME) )
			{
				fprintf(ourSuper.logFile,"Found home blocks at sectors 0x%08X, 0x%08X, 0x%08X\n",
						ourSuper.homeLbas[0],ourSuper.homeLbas[1],ourSuper.homeLbas[2]);
				fprintf(ourSuper.logFile, "Home block:\n");
				displayHomeBlock(ourSuper.logFile,&ourSuper.homeBlk,ckSum);
			}
			if ( getFileHeader("index.sys", &ourSuper, FSYS_ID_INDEX, (IndexSys_t *)ourSuper.homeBlk.index, &ourSuper.indexSysHdr) )
			{
				if ( (ourSuper.verbose&VERBOSE_HEADERS) )
					displayFileHeader(ourSuper.logFile, &ourSuper.indexSysHdr, 1 | (ourSuper.verbose & VERBOSE_RETPTRS));
				ourSuper.numInodesAvailable = (ourSuper.indexSysHdr.clusters * 512) / (FSYS_MAX_ALTS * sizeof(uint32_t));
				ourSuper.indexSys = (IndexSys_t *)calloc(ourSuper.indexSysHdr.clusters * 512, 1);
				if ( readWholeFile("index.sys", &ourSuper, (uint8_t*)ourSuper.indexSys, ourSuper.indexSysHdr.size, ourSuper.indexSysHdr.pointers[0]) < 0 )
				{
					fprintf(ourSuper.errFile,"Failed to read index.sys file\n");
					ret = -1;
					break;
				}
				if ( (ourSuper.verbose & VERBOSE_INDEX) )
				{
					if ( !(ourSuper.verbose&VERBOSE_HEADERS) )
						displayFileHeader(ourSuper.logFile,&ourSuper.indexSysHdr,1|(ourSuper.verbose&VERBOSE_RETPTRS));
					dumpIndex(ourSuper.logFile, ourSuper.indexSys, ourSuper.indexSysHdr.size);
				}
			}
			else
			{
				ret = -1;
				break;
			}
			/* First read all the fileheaders in the filesystem */
			/* Get some memory to hold all the local inodes */
			ourSuper.inodeList = (MgwfsInode_t **)calloc(ourSuper.numInodesAvailable, sizeof(MgwfsInode_t *));
			if ( !ourSuper.inodeList )
			{
				fprintf(ourSuper.errFile, "Sorry. Not enough memory to hold %d inode pointers (%ld bytes)\n", ourSuper.numInodesAvailable, sizeof(MgwfsInode_t *) * ourSuper.numInodesAvailable);
				close(ourSuper.fd);
				return 1;
			}
			inodePtr = ourSuper.inodeList;
			inode = (MgwfsInode_t *)calloc(1,sizeof(MgwfsInode_t));
			if ( !inode )
			{
				fprintf(ourSuper.errFile, "Sorry. Not enough memory to hold an inode for index.sys (%ld bytes)\n",
						sizeof(MgwfsInode_t));
				close(ourSuper.fd);
				return 1;
			}
			memcpy(&inode->fsHeader, &ourSuper.indexSysHdr, sizeof(FsysHeader));
			*inodePtr++ = inode;
			ret = 0;
			for (ii=1; ii < ourSuper.numInodesAvailable; ++ii)
			{
				char tmpName[32];
				IndexSys_t *lbas;
				
				inode = (MgwfsInode_t *)calloc(1,sizeof(MgwfsInode_t));
				if ( !inode )
				{
					fprintf(ourSuper.errFile, "Sorry. Not enough memory to hold an inode for file %d (%ld bytes)\n",
							ii, sizeof(MgwfsInode_t));
					close(ourSuper.fd);
					return 1;
				}
				*inodePtr++ = inode;
				lbas = ourSuper.indexSys + ii;
				if ( !lbas->lba[0] )
					break;
				snprintf(tmpName,sizeof(tmpName),"Inode %d", ii);
				if ( !(lbas->lba[0] & FSYS_EMPTYLBA_BIT) )
				{
					if ( getFileHeader(tmpName, &ourSuper, FSYS_ID_HEADER, lbas, &inode->fsHeader) )
					{
						inode->inode_no = ii;
						inode->mode = (inode->fsHeader.type == FSYS_TYPE_DIR) ? S_IFDIR|0555 : S_IFREG|0444;
						if ( (ourSuper.verbose&VERBOSE_HEADERS) )
							displayFileHeader(ourSuper.logFile, &inode->fsHeader, 1 | (ourSuper.verbose & VERBOSE_RETPTRS));
						else if ( (ourSuper.verbose&VERBOSE_MINIMUM) )
							fprintf(ourSuper.logFile, "Loaded file header (inode) %4d, lbas: 0x%08X 0x%08X 0x%08X. Type=0x%X (%s)\n",
								   ii,
								   lbas->lba[0], lbas->lba[1], lbas->lba[2],
								   inode->fsHeader.type,
								   S_ISDIR(inode->mode) ? "DIR":"REG");
						memcpy(inode->fhSectors,lbas,FSYS_MAX_ALTS*sizeof(uint32_t));
						++ourSuper.numInodesUsed;
					}
					else
					{
						ret = -1;
						break;
					}
				}
			}
			if ( ret < 0 )
				break;
			if ( (ourSuper.verbose&VERBOSE_MINIMUM) )
			{
				fprintf(ourSuper.logFile, "Inode info: inode size: %ld, inodesAvailable: %d, inodesUsed: %d\n", sizeof(MgwfsInode_t), ourSuper.numInodesAvailable, ourSuper.numInodesUsed);
			}
			/* The first 4 files don't actually belong to any directory and have no name, so fake it */
			inodePtr = ourSuper.inodeList;
			fLim = 4;
			if ( !ourSuper.homeBlk.journal[0] )
				fLim = 3;
			for ( ii = 0; ii < fLim; ++ii, ++inodePtr )
			{
				static const char * const Names[] = 
				{
					"index.sys", "freemap.sys", "rootdir.sys", "journal.sys"
				};
				inode = *inodePtr;
				strncpy(inode->fileName, Names[ii], MGWFS_FILENAME_MAXLEN);
				inode->fnLen = strlen(inode->fileName);
				inode->idxParentInode = FSYS_INDEX_ROOT;
				inode->inode_no = ii;
				inode->mode = (inode->fsHeader.type == FSYS_TYPE_DIR) ? S_IFDIR | 0555 : S_IFREG | 0444;
				/* But we need to read the contents of the freemap file */
				if ( ii == FSYS_INDEX_FREE )
				{
					int jj;
					FreeMap_t *freeMap = &ourSuper.freeMap;
					FsysRetPtr *rp;
					freeMap->rwBuff.buff = (uint8_t *)calloc(inode->fsHeader.clusters, 512);
					freeMap->freeMapEntriesAvail = (inode->fsHeader.clusters*512 + sizeof(FsysRetPtr) - 1) / sizeof(FsysRetPtr);
					if ( readWholeFile("freemap.sys", &ourSuper, freeMap->rwBuff.buff, inode->fsHeader.size, inode->fsHeader.pointers[0]) < 0 )
					{
						fprintf(ourSuper.errFile,"Failed to read freemap.sys file\n");
						ret = -1;
						break;
					}
					rp = (FsysRetPtr *)freeMap->rwBuff.buff;
					for ( jj = 0; rp->nblocks && jj < freeMap->freeMapEntriesAvail; ++jj, ++rp )
						++freeMap->freeMapEntriesUsed;
					if ( (ourSuper.verbose & (VERBOSE_FREEMAP | VERBOSE_VERIFY_FREEMAP)) )
					{
						if ( (ourSuper.verbose & VERBOSE_VERIFY_FREEMAP) )
						{
							options.quit = 1;
							verifyFreemap(&ourSuper);
						}
					}
					else if ( (ourSuper.verbose&VERBOSE_MINIMUM) )
					{
						fprintf(ourSuper.logFile, "Loaded %ld slots (%d bytes) of freemap\n",
							   (inode->fsHeader.clusters * 512)/sizeof(FsysRetPtr),
							   inode->fsHeader.clusters * 512
							   );
					}
				}
			}
			if ( ret < 0 )
				break;
			inode = ourSuper.inodeList[FSYS_INDEX_ROOT]; /* Point to the root directory */
			inode->idxParentInode = FSYS_INDEX_ROOT;
			unpackDir(&ourSuper, inode, 0); /* Create the entire filesystem directory tree */
			if ( (ourSuper.verbose&VERBOSE_ITERATE) )
				tree(&ourSuper, FSYS_INDEX_ROOT, 0 );
		} while ( 0 );
	}
	if ( ret >= 0 && options.testPath )
	{
		int idx = findInode(&ourSuper,FSYS_INDEX_ROOT,options.testPath);
		fprintf(ourSuper.logFile,"getInode('%s') returned %d\n", options.testPath, idx);
	}
	fflush(ourSuper.logFile);
	if ( ret >= 0 && !options.quit )
	{
		ret = fuse_main(args.argc, args.argv, &mgwfs_oper, NULL);
		fuse_opt_free_args(&args);
	}
	if ( options.logFile )
		fclose(ourSuper.logFile);
	if ( ourSuper.fd >= 0 )
		close(ourSuper.fd);
	if ( ourSuper.indexSys )
		free( ourSuper.indexSys );
	if ( (inodePtr = ourSuper.inodeList) )
	{
		for (ii=0; ii < ourSuper.numInodesAvailable; ++ii, ++inodePtr)
		{
			if ( *inodePtr )
				free(*inodePtr);
			*inodePtr = NULL;
		}
		free(ourSuper.inodeList);
	}
#if !NO_MUTEXES
	mgwfs_destroy_mutex();
	fuse_destroy_mutex();
#endif
	return ret;
}
