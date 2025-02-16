/*
  mgwfsf: Atari/MidwayGamesWest filesystem using libfuse: Filesystem in Userspace

  Copyright (C) 2025  Dave Shepperd <mgwfsf@dshepperd.com>

  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.

 Build with enclosed Makefile

*/

#include "mgwfsf.h"

static void helpEm(FILE *ofp, const char *progname)
{
	fprintf(ofp, "Usage: %s [options] <mountpoint>\n", progname);
	fprintf(ofp, "Filesystem specific options:\n"
		   "--log=<path>    Specify a path to a logfile (default=stdout)\n"
		   "--image=<path>  Specify a path to filesystem file (required)\n"
		   "--testpath=<path> Specify a test path into filesystem file (forces a -q)\n"
		   "--verbose=n 'n' is bit mask of verbose modes:\n"
		   "            May be expressed with normal C syntax [i.e. prefix 0x or 0b for hex or binary]:\n"
		   );
	fprintf(ofp, "    0x%04X = display some small details\n", VERBOSE_MINIMUM);
	fprintf(ofp, "    0x%04X = display home block\n", VERBOSE_HOME);
	fprintf(ofp, "    0x%04X = display file headers\n", VERBOSE_HEADERS);
	fprintf(ofp, "    0x%04X = display all retrieval pointers in file headers\n", VERBOSE_RETPTRS);
	fprintf(ofp, "    0x%04X = display attempts at file reads\n", VERBOSE_READ);
	fprintf(ofp, "    0x%04X = display contents of index.sys file\n", VERBOSE_INDEX);
	fprintf(ofp, "    0x%04X = display freemap primitive actions\n", VERBOSE_FREE);
	fprintf(ofp, "    0x%04X = display contents of freemap.sys file\n", VERBOSE_FREEMAP);
	fprintf(ofp, "    0x%04X = display and verify contents of freemap.sys file\n", VERBOSE_VERIFY_FREEMAP);
	fprintf(ofp, "    0x%04X = display contents of rootdir.sys file\n", VERBOSE_DMPROOT);
	fprintf(ofp, "    0x%04X = display details during unpack()\n", VERBOSE_UNPACK);
	fprintf(ofp, "    0x%04X = display directory searches \n", VERBOSE_LOOKUP);
	fprintf(ofp, "    0x%04X = display details during directory searches\n", VERBOSE_LOOKUP_ALL);
	fprintf(ofp, "    0x%04X = display directory tree\n", VERBOSE_ITERATE);
	fprintf(ofp, "    0x%04X = display anything FUSE related\n", VERBOSE_FUSE);
	fprintf(ofp, "    0x%04X = display FUSE function calls\n", VERBOSE_FUSE_CMD);
	fprintf(ofp, "-q        = quit before starting fuse stuff\n");
	fprintf(ofp, "-v        = sets verbose flag to a value of 0x001\n");
}

#define OPTION(t, p )                           \
    { t, offsetof(Options_t, p), 1 }
	
static const struct fuse_opt option_spec[] =
{
	OPTION( "--allocation=%lu", allocation ),
	OPTION( "--copies=%lu", copies ),
	OPTION( "--image=%s", image ),
	OPTION( "--testpath=%s", testPath ),
	OPTION( "--log=%s", logFile ),
	{ "--verbose", -1, FUSE_OPT_KEY_OPT},
	OPTION("-v", verbose ),
	OPTION("-h", show_help ),
	OPTION("--help", show_help ),
	OPTION("-q", quit ),
	OPTION("--quit", quit ),
	OPTION("--version", show_version ),
	FUSE_OPT_END
};

static int procOption(void *data, const char *arg, int key, struct fuse_args *outargs)
{
	int ret=1;
	static const char Verbose[] = "--verbose";
	int sLen=sizeof(Verbose)-1;
	
//	printf("ProcOption('%s') checking for match\n", arg);
	if ( !strncmp(arg,Verbose,sLen) )
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

int main(int argc, char *argv[])
{
	int ii;
	int ret;
	uint32_t homeLbas[FSYS_MAX_ALTS], ckSum;
	MgwfsInode_t *inode;
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
	}
	else
		ourSuper.logFile = stdout;
	fprintf(ourSuper.logFile, "Allocation=%ld, copies=%ld, verbose=0x%lX, imageName=%s, quit=%ld, logFile='%s'\n",
		   options.allocation, options.copies, options.verbose, options.image, options.quit, options.logFile);
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
			ret = stat(ourSuper.imageName, &st);
			if ( ret < 0 )
			{
				fprintf(stderr,"Unable to stat '%s': %s\n", ourSuper.imageName, strerror(errno));
				break;
			}
			ourSuper.fd = open(ourSuper.imageName, O_RDONLY);
			if ( ourSuper.fd < 0 )
			{
				fprintf(stderr, "Error opening the '%s': %s\n", ourSuper.imageName, strerror(errno));
				ret = -1;
				break;
			}
	
			sizeInSectors = st.st_size/512;
			maxHb = sizeInSectors > FSYS_HB_RANGE ? FSYS_HB_RANGE:sizeInSectors;
			if ( (ourSuper.verbose&VERBOSE_HOME) )
			{
				fprintf(ourSuper.logFile, "File size 0x%lX, maxSector=0x%lX, maxHb=0x%lX\n", st.st_size, sizeInSectors, maxHb);
				fprintf(ourSuper.logFile, "Attempting to read a partition table that might be present\n");
			}
			if ( (sizeof(bootSect) != read(ourSuper.fd,&bootSect,sizeof(bootSect))) )
			{
				fprintf(stderr, "Failed to read boot sector: %s\n", strerror(errno));
				ret = -2;
				break;
			}
			for (ii=0; ii < 4; ++ii)
			{
				if ( bootSect.parts[ii].status == 0x80 && bootSect.parts[ii].type == 0x8f )
				{
					ourSuper.baseSector = bootSect.parts[ii].abs_sect;
					sizeInSectors = bootSect.parts[ii].num_sects;
					maxHb = sizeInSectors > FSYS_HB_RANGE ? FSYS_HB_RANGE:sizeInSectors;
					if ( (ourSuper.verbose&VERBOSE_HOME) )
						fprintf(ourSuper.logFile, "Found an agc fsys partition in partition %d. baseSector=0x%X, numSectors=0x%lX, maxHb=0x%lX\n", ii, ourSuper.baseSector, sizeInSectors, maxHb);
					break;
				}
			}
			if ( (ourSuper.verbose&VERBOSE_HOME) && ii >= 4 )
				fprintf(ourSuper.logFile, "No parition table found\n");
			for (ii=0; ii < FSYS_MAX_ALTS; ++ii)
				homeLbas[ii] = FSYS_HB_ALG(ii, maxHb);
			if ( !getHomeBlock(&ourSuper,homeLbas,maxHb,sizeInSectors,&ckSum) )
			{
				ret = -1;
				break;
			}
			if ( (ourSuper.verbose&VERBOSE_HOME) )
			{
				fprintf(ourSuper.logFile, "Home block:\n");
				displayHomeBlock(ourSuper.logFile,&ourSuper.homeBlk,ckSum);
			}
			if ( getFileHeader("index.sys", &ourSuper, FSYS_ID_INDEX, ourSuper.homeBlk.index, &ourSuper.indexSysHdr) )
			{
				if ( (ourSuper.verbose&VERBOSE_HEADERS) )
					displayFileHeader(ourSuper.logFile, &ourSuper.indexSysHdr, 1 | (ourSuper.verbose & VERBOSE_RETPTRS));
				ourSuper.numInodesAvailable = (ourSuper.indexSysHdr.clusters * 512) / (FSYS_MAX_ALTS * sizeof(uint32_t));
				ourSuper.indexSys = (uint32_t *)calloc(ourSuper.indexSysHdr.clusters * 512, 1);
				if ( readFile("index.sys", &ourSuper, (uint8_t*)ourSuper.indexSys, ourSuper.indexSysHdr.size, ourSuper.indexSysHdr.pointers[0]) < 0 )
				{
					fprintf(stderr,"Failed to read index.sys file\n");
					ret = -1;
					break;
				}
				if ( (ourSuper.verbose & VERBOSE_INDEX) )
				{
					if ( !(ourSuper.verbose&VERBOSE_HEADERS) )
						displayFileHeader(ourSuper.logFile,&ourSuper.indexSysHdr,1|(ourSuper.verbose&VERBOSE_RETPTRS));
					dumpIndex(ourSuper.indexSys, ourSuper.indexSysHdr.size);
				}
			}
			else
			{
				ret = -1;
				break;
			}
			/* First read all the fileheaders in the filesystem */
			/* Get some memory to hold all the local inodes */
			ourSuper.inodeList = (MgwfsInode_t *)calloc(ourSuper.numInodesAvailable, sizeof(MgwfsInode_t));
			if ( !ourSuper.inodeList )
			{
				fprintf(stderr, "Sorry. Not enough memory to hold %d inodes (%ld bytes)\n", ourSuper.numInodesAvailable, sizeof(MgwfsInode_t) * ourSuper.numInodesAvailable);
				close(ourSuper.fd);
				return 1;
			}
			inode = ourSuper.inodeList;
			memcpy(&inode->fsHeader, &ourSuper.indexSysHdr, sizeof(FsysHeader));
			++inode;
			ret = 0;
			for (ii=1; ii < ourSuper.numInodesAvailable; ++ii, ++inode)
			{
				char tmpName[32];
				uint32_t *lbas;
				
				lbas = ourSuper.indexSys + ii * FSYS_MAX_ALTS;
				if ( !*lbas )
					break;
				snprintf(tmpName,sizeof(tmpName),"Inode %d", ii);
				if ( !(*lbas & FSYS_EMPTYLBA_BIT) )
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
								   lbas[0], lbas[1], lbas[2],
								   inode->fsHeader.type,
								   S_ISDIR(inode->mode) ? "DIR":"REG");
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
			inode = ourSuper.inodeList;
			for (ii=0; ii < 4; ++ii, ++inode)
			{
				static const char * const Names[] = 
				{
					"index.sys", "freemap.sys", "rootdir.sys", "journal.sys"
				};
				strncpy(inode->fileName, Names[ii], sizeof(inode->fileName));
				inode->fnLen = strlen(inode->fileName);
				inode->idxParentInode = FSYS_INDEX_ROOT;
				inode->inode_no = ii;
				inode->mode = (inode->fsHeader.type == FSYS_TYPE_DIR) ? S_IFDIR | 0555 : S_IFREG | 0444;
				/* But we need to read the contents of the freemap file */
				if ( ii == FSYS_INDEX_FREE )
				{
					ourSuper.freeMap = (FsysRetPtr *)calloc(inode->fsHeader.clusters * 512, 1);
					if ( readFile("freemap.sys", &ourSuper, (uint8_t *)ourSuper.freeMap, inode->fsHeader.size, inode->fsHeader.pointers[0]) < 0 )
					{
						fprintf(stderr,"Failed to read freemap.sys file\n");
						ret = -1;
						break;
					}
					if ( (ourSuper.verbose & (VERBOSE_FREEMAP | VERBOSE_VERIFY_FREEMAP)) )
					{
						dumpFreemap("Contents of freemap.sys before merge:", ourSuper.freeMap, (inode->fsHeader.size + sizeof(FsysRetPtr) - 1) / sizeof(FsysRetPtr));
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
			inode = ourSuper.inodeList + FSYS_INDEX_ROOT; /* Point to the root directory */
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
	if ( ret >= 0 && !options.quit )
	{
		ret = fuse_main(args.argc, args.argv, &mgwfsf_oper, NULL);
		fuse_opt_free_args(&args);
	}
	if ( options.logFile )
		fclose(ourSuper.logFile);
	if ( ourSuper.fd >= 0 )
		close(ourSuper.fd);
	if ( ourSuper.indexSys )
		free( ourSuper.indexSys );
	if ( ourSuper.inodeList )
		free(ourSuper.inodeList);
	return ret;
}
