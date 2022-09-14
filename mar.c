/*
 *	MSDOS archive/transfer program.
 *	reads, writes and lists MSDOS format disks.
 *
 *	AUTHOR: Clifford Heath, Hewlett-Packard Australian Software Operation.
 *		{hplabs!hpfcla,hpbbn}!hpausla!cjh
 *	(c)Copyright 1985, 1986.
 *
 *	No warranties express or implied are made concerning the usefulness
 *	validity or ability of this software for any purpose whatsoever.
 *
 *	This program was written on an HP9000 Series 500 in three days (That's
 *	right, about 20 hours) and subsequently was ported to a Series 200
 *	where it compiled and ran correctly the first time. It is fairly
 *	portable, but watch byte swapping:
 *
 *	PORTING:
 *
 *	You may not have strchr. Include -Dstrchr=index in CFLAGS.
 *
 *	All directories have short fields which are byte swapped and long
 *	fields which are byte reversed on reading the directory, and the same
 *	operation on writing. This may need to be changed depending on the byte
 *	order on your machine. It is done by the routine fixdir(). To check,
 *	just compile mar, do a 't' on a valid MS-DOS volume and check the file
 *	sizes. If the sizes are wrong, try using -DNOSWAB to cc. Otherwise
 *	it's over to you.
 *
 *	Check also the order in which your compiler allocates bit-fields.
 *	If it is different from ours (MSB first) then dates and times will
 *	be wrong. Edit the 'struct dir' definition to reverse the declarations.
 *
 *
 *	Usage:
 *	mar <command> device/file [ file/directory ...]
 *
 *	<command> is some combination of characters from the lists given of
 *	Commands, Flags and Disk types, with no intervening spaces.
 *
 *	File/directory names are UNIX pathnames, without leading '/'
 *
 *	If device/file doesn't exist, a query will determine whether it is
 *	to be created.
 *
 *	Commands:
 *	c	format disk (clobber all existing files)
 *		implies 'r' if files are specified.
 *		This will always elicit a query.
 *	r	replace files onto disk, creating MSDOS directories as necessary
 *		If a filename given is a UNIX directory, an empty MSDOS
 *		directory will be created.
 *	t	list files on disk. If no files are specified, list whole disk.
 *		Without 'v', gives pathnames only.
 *		With 'v', gives attributes (hidden, system, directory, readonly)
 *			time, date, size and filename.
 *	x	extract files from disk. Directories are extracted recursively.
 *	d	delete files from disk. If no files are specified, this is 'c'.
 *
 *	Flags:
 *	v	verbose. For rxc, says which files; for t, gives size, date etc.
 *	a	ascii. Convert line end characters from/to \r\n <--> \n.
 *
 *	Disk types (as in "dtypes" table below)
 *	mar knows how to access all HP150 disk types:
 *
 *	m	3 1/2" single sided micro- or 5 1/4" mini- floppy (DEFAULT)
 *	M	3 1/2" double sided micro floppy
 *	f	8" IBM format floppy
 *	F	8" HP floppy
 *	e	5 Mb Winchester
 *	j	10 Mb Winchester
 *	o	15 Mb Winchester
 *
 *	User defined disk format:
 *	pI,J,K,L,M,N,O,P
 *		I = Sector size in bytes.
 *		J = Number of sectors in a cluster.
 *		K = Number of sectors before first sector of first FAT.
 *		M = Number of sectors in root directory.
 *		N = Number of sectors in a single FAT.
 *		O = Number of copies of the FAT.
 *		P = Number of CLUSTERS after boot area, FATs and root dir.
 *
 *	Not yet implemented:
 *		Non recursive directory list
 *		Recursive replace. ('R')
 *		Command-line specification of different disk formats.
 *
 *	Implemented but not tested:
 *
 *	Implemented and tested:
 *		listing whole device
 *		listing whole directory subtrees.
 *		listing individual files.
 *
 *		file extraction, including creation of directories.
 *		recursive extraction of whole subtrees or whole device
 *
 *		file deletion.		
 *		directory deletion.
 *
 *		formatting disk (fat, root dir, etc)
 *
 *		replacing files onto disks
 *		handling write errors on replace
 *		creating directories for replace
 */
#include	<stdio.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<time.h>
#include	<errno.h>
#include	<string.h>
#include	<stdlib.h>

typedef struct
{
	char	name[11];
	char	attr;
	char	fill[10];

#define	SWABFROM	fill[10]		/* swap bytes from here */
	short	hour:5;		/* 0-23 */
	short	minute:6;	/* 0-59 */
	short	second:5;	/* Seconds/2 */

	short	year:7;		/* Year - 1980 */
	short	month:4;	/* 1-12 */
	short	day:5;		/* 1-31 */

	short	start;		/* Starting cluster */
	union {
		long	lsize;
		short	ssize[2];	/* for swapping */
	}	s;
#define	size	s.lsize
#define	SWABTO		size			/* to here */
}
	dir;

/*
 *	Bits in attr
 */
#define	RONLY	0x1		/* Readonly bit */
#define	HIDDEN	0x2
#define	SYSTEM	0x4
#define	VOLUME	0x8		/* Not a file but a volume label */
#define	DIRECT	0x10		/* Subdirectory */
#define	ARCHIVE	0x20		/* Has been modified since full backup */

/*
 *	Format etc of types of disks
 *	Should be some way of specifying this from the command line ???
 */
#define	SECSIZE	(dtypes[dtype].secsize)
#define	CLUSIZE	(dtypes[dtype].clusize*SECSIZE)
#define	FAT1	(dtypes[dtype].fat1*SECSIZE)
#define	NDIR	(dtypes[dtype].rootdirs*SECSIZE/sizeof(dir))
#define	FATSIZE	(dtypes[dtype].fatsize*SECSIZE)
#define	NFAT	(dtypes[dtype].nfat)
#define	NCLUS	(dtypes[dtype].nclus)
#define	DPCLUS	(CLUSIZE/sizeof(dir)) /* Directory entries per cluster */

int	dtype;
struct	disk
{
	char	type;
	char	*tname;
	int	secsize;		/* Size of a sector */
	int	clusize;		/* Bytes in a cluster */
	int	fat1;			/* address of fat 1 */
	int	rootdirs;		/* Number of entries in root dir */
	int	fatsize;		/* file allocation table size */
	int	nfat;			/* Number of copies of the fat */
	int	nclus;			/* Number of data clusters */
}	dtypes[] =
{
	{		/* 3 1/2" microfloppy, single sided */
			/* 5 1/4" minifloppy double sided */
		'm',	"3 1/2\" single sided micro- or 5 1/4\" mini- floppy",
		256,	4,	2,	16,	3,	2,	260
	},
	{		/* 3 1/2" microfloppy, double sided */
		'M',	"3 1/2\" double sided micro floppy",
		512,	4,	2,	8,	3,	2,	686
	},
	{		/* 8" floppy IBM format */
		'f',	"8\" IBM format floppy",
		128,	8,	1,	17,	6,	2,	249
	},
	{		/* 8" floppy Hewlett-Packard format */
		'F',	"8\" HP floppy",
		256,	16,	2,	32,	3,	1,	280
	},
	{		/* 5 Mb Winchester */
		'e',	"5 Mb Winchester",
		256,	16,	2,	128,	9,	2,	545
	},
	{		/* 10 Mb Winchester */
		'j',	"10 Mb Winchester",
		256,	16,	2,	128,	15,	2,	2355
	},
	{		/* 15 Mb Winchester */
		'o',	"15 Mb Winchester",
		256,	16,	2,	128,	21,	2,	3536
	},
	{		/* User defined disk */
		'p',	"User defined disk format",
		0,	0,	0,	0,	0,	0,	0
	},
	{	0	}
};

extern	int	errno;
int	clobber = 0, verbose = 0, binary = 1;
int	nfiles;
char	cmd = 0;
char	*device;
char	**files;
void	opendevice(), erexit(), forall(), show(), replace(), makedir(),
	makeent(), extract(), extrall(), do_extract(), delete(), listdir(),
	putdir(), truncate(), putfat(), dos_format(), dos_end(), myswab(),
	readboot(), showboot(), writeboot(), hex_dump();

int	disk;
int	getdir_num;
long	rootaddr;		/* Address of start of root directory */
long	database;		/* Address of start of data */
dir	*rootdir;
int	root_mod;		/* Root directory has been modified */
int	fat_mod;		/* File allocation table has been modified */
dir	*dirblk;
char	*fat;

dir	*getdisk();
dir	*getdir();
dir	*fixdir();
char	*fixname();
long	diskfree();
char	*Malloc();
char	*strchr();
struct	tm	*localtime();

void
erexit(s,a)
char	*s;
char	*a;
{
	fprintf(stderr,s,a);
	exit(1);
}

main(argc,argv)
char **argv;
{
	register	i, j;
	char		*p = argv[1];

	if (argc < 3)
		erexit("Usage: %s [tcrxd][v] device [file ...]\n",argv[0]);
	device = argv[2];
	files = argv+3;
	nfiles = argc-3;

	while (*p) switch(*p++) {
	case 't':	/* List */
	case 'x':	/* Extract */
	case 'r':	/* Replace */
	case 'd':	/* Delete */
		if (cmd)
			erexit("Only one of [tcrxd] may be specified\n", 0);
		cmd = p[-1];
		break;
	case 'v':
		verbose++;
		break;
	case 'a':
		binary = 0;
		break;
	case 'c':
		clobber++;
		break;
	case '-': continue;

	default:
		for (dtype = 0; dtypes[dtype].type != '\0'; dtype++)
			if (p[-1] == dtypes[dtype].type)
				break;
		if (dtypes[dtype].type == 'p')
		{
			for (i = 0; i < 7; i++)
			{
				if ((j = myatoi(&p)) < 0)
				{
					printf("Bad number in p option (7 required)\n");
					exit(1);
				}

				switch (i)
				{
				case 0:	dtypes[dtype].secsize = j; break;
				case 1:	dtypes[dtype].clusize = j; break;
				case 2:	dtypes[dtype].fat1 = j; break;
				case 3:	dtypes[dtype].rootdirs = j; break;
				case 4:	dtypes[dtype].fatsize = j; break;
				case 5:	dtypes[dtype].nfat = j; break;
				case 6:	dtypes[dtype].nclus = j; break;
				}

				if (i < 6)
					while (*p != '\0' && (*p<'0' || *p>'9'))
						p++;	/* Skip separators */
			}
		}
		if (dtypes[dtype].type != '\0')
		{
			if (verbose)
				printf("%s\n",dtypes[dtype].tname);
			continue;
		}
		printf("Unknown option: %c\n",p[-1]);
		exit(0);
	}

	if (cmd == 0)
	{
		if (clobber)
			cmd = 'r';
		else
			erexit("One of [tcrxd] must be specified\n", 0);
	}
	if (!nfiles && cmd == 'd') {
		clobber++;
		cmd = 'c';
	}
	opendevice();

	switch (cmd) {
	case 't': listdir("",rootdir, NDIR); break;
	case 'r': forall(replace); break;
	case 'd': forall(delete); break;
	case 'x': if (nfiles)
			forall(extract);
		  else
			extrall("",rootdir,NDIR);
		  break;
	}
	dos_end();
	exit(0);
	/*NOTREACHED*/
}

/*
 *	Simple atoi for +ve numbers that advances the pointer.
 */
myatoi(pp)
char	**pp;
{
	register 	i = 0;

	if (**pp < '0' || **pp > '9')
		return -1;
	while (**pp >= '0' && **pp <= '9')
	{
		i = i*10 + **pp-'0';
		(*pp)++;
	}
	return i;
}

void
opendevice()
{
	register dir	*vol;
	register mode = 0;

	if (cmd == 'c' || cmd == 'd' || cmd == 'r')
		mode = 2;
	if ((disk = open(device,mode)) < 0) {
		if (!mode || errno != ENOENT) {
		pe:	perror(device);
			exit(1);
		}
		printf("Create %s (y or n) ?",device);
		if (getchar() != 'y')
			erexit("abort\n", 0);
		while(getchar() != '\n');
		close(disk);
		if ((disk = creat(device,0666)) < 0)
			goto pe;
		close(disk);
		if ((disk = open(device,mode)) < 0)
			goto pe;
		dos_format();
		clobber++;
	} else {
		if (clobber) {
			printf("Really clobber %s \7(y or n) ?",device);
			if (getchar() != 'y')
				erexit("aborted\n", 0);
			while(getchar() != '\n');
			dos_format();
		} else
		{
			vol = getdisk();
			if (vol != NULL
			 && verbose
			 && !nfiles
			 && cmd == 't')
				printf("%s:\n",fixname(vol->name));
		}
	}
}

void
forall(f)
int	(*f)();
{
	while (nfiles--)
		(*f)(*files++);
}

ucase(c)
{
	if (c >= 'a' && c <= 'z')
		return c+'A'-'a';
	return c;
}

lcase(c)
{
	if (c >= 'A' && c <= 'Z')
		return c+'a'-'A';
	return c;
}

void
show(c,f)
char	c, *f;
{
	if (verbose)
		printf("%c - %s\n",c,f);
}

void
replace(f)
char	*f;
{
	register start;
	register fd, r;
	register char	*p, *q;
	char	op = 'r';	/* May get changed to 'u' */
	int	ret = 0;
	char	*namepart = f;
	char	*end;
	int	num = NDIR;
	dir	*dp = rootdir;
	dir	*dirp = rootdir;
	char	*buf;
	char	*buf1;
	int	clus = 0, new;
	long	df;
	long	a;
	struct	stat	sb;
	long	new_size;

	/*
	 *	Make sure we can access the file, and get some info
	 */
	if (access(f,04) != 0
	 || stat(f,&sb) != 0)
	{
		perror(f);
		return;
	}
	new_size = sb.st_size;

 again:
	end = strchr(namepart,'/');
	if (end != NULL)
		*end = '\0';
	/*
	 *	Search for the file/subdirectory 'namepart'
	 */
	for (; dp < dirp+num; dp++)
	{
		if (dp->name[0] == 0)
			break;
		if (strcmp(namepart,fixname(dp->name)))
			continue;
		/*
		 *	Found current part of pathname
		 */
		if (dp->attr&DIRECT)
		{
			if (end == NULL)
			{
				if ((sb.st_mode&S_IFMT) != S_IFDIR)
					printf("%s: Directory in path\n",f);
				else if (verbose)
					printf("%s: Directory exists\n",f);
				*end = '/';	/* Restore the / */
				if (dirp != rootdir)
					free(dirp);
				return;
			}
			/*
			 *	Load in the directory
			 */
			start = dp->start;
			if (dirp != rootdir)
				free(dirp);
			dirp = dp = getdir(start);
			num = getdir_num;
			*end = '/';	/* Restore the / */
			namepart = end+1;
			goto again;
		}
		/* It's a file */
		if (end != NULL)
		{
			/* ... but we expect a directory */
			printf("%s is not a directory\n",f);
			*end = '/';
			if (dirp != rootdir)
				free(dirp);
			return;
		}
		else
		{
			/*
			 *	The file already exists
			 *	Delete it, then put new one.
			 */
			op = 'u';
			truncate(dp->start);
			dp->name[0] = 0xE5;
			break;
		}
	}

	df = diskfree();

	if (df < new_size)
		/*
		 *	May not be true if !binary
		 *	and file is mostly consecutive \r's,
		 *	but never mind about that!
		 *	It may save reading the file in this mode.
		 */
		goto room;

	if ((fd = open(f,0)) < 0)
		/* We checked for this before */
		erexit("%s: Impossible open error\n",f);

	buf = Malloc(CLUSIZE);
	if (!binary)
		buf1 = Malloc(CLUSIZE);

	/*
	 *	If we are going to insert '\r's, work out new file size.
	 */
	if (!binary)
	{
		new_size = 0;
		while ((r = read(fd,buf,CLUSIZE)) > 0)
		{
			for (p = buf; p < buf+r; p++)
			{
				if (*p == '\n' && !ret)
				{
					new_size++;
					p--;	/* Don't advance p */
					ret = 1;
				}
				else if (*p == '\r')
				{
					ret = 0;
					new_size++;
				}
				else
					new_size++;
			}
		}

		if (r < 0)
		{			/* Read error */
			perror(f);
			goto pd;
		}

		lseek(fd, 0L, 0);	/* Back to start */
	}

	if (df < new_size)
		goto room;

	/*
	 *	Find a slot
	 */
	for (dp = dirp; dp <= dirp+num; dp++)
		if (dp->name[0] == (char)0xE5 || dp->name[0] == 0)
			break;
	if (dp == dirp+num)
	{		/* Want to grow directory */
		if (df < new_size+CLUSIZE)
			goto room;	/* Can't grow directory */
		if (dirp == rootdir)
		{
			printf("%s: No more room in root directory\n",f);
			goto pd;
		}
	}

	/*
	 *	Put the file if there's room to do it.
	 */
	if (df < new_size)
	{
	room:	printf("No room to add file %s\n",f);
		goto pd;
	}

	/* After this, we really ought to be able to make a decent attempt */
	if (dp == dirp+num)
		num++;		/* Grow the directory */

	/* Build the directory entry */
	makeent(dp,namepart,&sb);

	/*
	 *	If what we want is a subdirectory, make it.
	 */
	if (dp->attr&DIRECT || end != NULL)
	{
		register char *cp;
		dir	*newdir;

		/*
		 *	Create empty directory and move to it
		 */
		dp->attr |= DIRECT;
		newdir = (dir *)Malloc(CLUSIZE);
		makedir(newdir,".");
		if ((newdir->start = getfree()) == 0)
		{
			free(newdir);
			dp->name[0] = 0xE5; /* Delete the entry for the dir */
			goto room;
		}
		/* Mark the block used */
		putfat(dp->start = newdir->start, 0xFF8);
		makedir(newdir+1,"..");
		newdir[1].start = dp->start;

		/* finish with parent */
	 	putdir(start,dirp,num);
		if (dirp != rootdir)
			free(dirp);
		/* Move to new */
		dp = dirp = newdir;
		num = DPCLUS;
		start = newdir->start;
		/*
		 *	Initialize remaining entries properly
		 */
		cp = (char *)(newdir+2);
		while (cp < (char *)newdir+CLUSIZE)
			*cp++ = 0xE5;

		for (dp = newdir+2; dp < rootdir+DPCLUS; dp++)
			dp->name[0] = 0;
		dp = dirp;
		if (end != NULL)
		{		/* Made a directory inside path */
			/*
			 *	Write it out, so that if something bad happens
			 *	it's at least consistent
			 */
			putdir(start,dirp,num);
			/*
			 *	putdir fixed it for writing;
			 *	fix it back again
			 */
			fixdir(dirp,num);
			*end = '/';
			namepart = end+1;
			goto again;
		}
		else
		{		/* Finished - just directory was specified */
			show('r',f);
			goto pd;
		}
	}

	show(op,f);
	if (new_size == 0)
		goto pd;		/* Zero size file */

	dp->start = 0;		/* Say no clusters allocated yet */
	q = buf1;
	ret = 0;
	a = 0;
	while ((r = read(fd,buf,CLUSIZE)) > 0)
	{
		if (r < CLUSIZE && binary)
		{		/* Null pad buffer */
			p = buf+r;
			while (p < buf+CLUSIZE)
				*p++ = 0;
		}
		if (binary)
		{		/* No crushing of cr-nl's */
		newclus1:
			new = getfree();
			if (!new)
			{
		quit:
			    printf("%s: Out of space due to bad blocks\n",f);
			    break;
			}
			if (!writeclus(new,buf))
			{		/* Write error - mark cluster bad */
				printf("Marking cluster bad\n");
				putfat(new,0xFF7);
				goto newclus1;
			}
			if (dp->start)
				putfat(clus,new);
			else
				dp->start = new;
			putfat(new,0xFF8);	/* Mark eof */
			clus = new;
			a += r;
			continue;
		}
		/*
		 *	Pack buf1, inserting '\r' before '\n'
		 */
		for (p = buf; p < buf+r;) {
			/*
			 *	When *p is a '\n',
			 *	we will go around this loop twice for it.
			 *	The first time (ret = 0), we add the \r,
			 *	the second time (ret = 1), we add the \n.
			 */
			if (*p == '\n' && !ret)
				*q++ = '\r';
			else
				*q++ = *p++;
			if (q[-1] == '\r')
				ret = 1;
			else
				ret = 0;
			/* Time to flush output buffer ? */
			if (q == buf1+CLUSIZE) {
			newclus2:
				new = getfree();
				if (!new)
					goto quit;
				if (!writeclus(new,buf1))
				{	/* Write error - mark cluster bad */
					printf("Marking cluster bad\n");
					putfat(new,0xFF7);
					goto newclus2;
				}
				if (dp->start)
					putfat(clus,new);
				else
					dp->start = new;	/* First clus */
				putfat(clus = new,0xFF8);	/* Mark eof */
				a += CLUSIZE;
				q = buf1;
			}
		}
	}

	if (r < 0)
	{		/* Read error */
		perror(f);
		goto pd;
	}

	/* set size written field */
	if (!binary)
		dp->size = a+q-buf1;
	else
		dp->size = a;

	if (!binary && q != buf1)
	{		/* flush buf1 */
		while (q < buf1+CLUSIZE)
			*q++ = 0;	/* Null pad */
		new = getfree();
		if (new)
		{
			if (dp->start)
				putfat(clus,new);
			else
				dp->start = new;
			putfat(clus = new,0xFF8);	/* Mark eof */
			if (!writeclus(clus,buf1))
				printf("Ignored\n");
		}
		else
			printf("%s: Out of space due to bad blocks\n",f);
	}
	free(buf);
	if (!binary)
		free(buf1);

	/*
	 *	Write out the directory
	 */
 pd:	putdir(start,dirp,num);
	if (dirp != rootdir)
		free(dirp);
}

/*
 *	This makes the entries for . and ..
 */
void
makedir(dp,name)
dir	*dp;
char	*name;
{
	register char	*p;

	/* Build the MSDOS name */
	for (p = dp->name; p < dp->name+11;)
		if (*name != '\0')
			*p++ = *name++;
		else
			*p++ = ' ';

	dp->attr = DIRECT;
	for (p = dp->fill; p < dp->fill+10; *p++ = 0)
		;
	dp->hour = 0;
	dp->minute = 0;
	dp->second = 0;
	dp->year = 0;
	dp->month = 0;
	dp->day = 0;
	dp->size = 0;
	dp->start = 0;
}

/*
 *	Do the hack work in building a directory entry
 */
void
makeent(dp,name,sb)
dir	*dp;
char	*name;
struct	stat	*sb;
{
	register char	*p;
	struct	tm	*tm;

	/* Build the MSDOS name */
	for (p = dp->name; p < dp->name+11;)
	{
		if (p == dp->name+8 && *name == '.')
			name++;
		if (*name != '\0' && *name != '.')
			*p++ = ucase(*name++);
		else
			*p++ = ' ';
	}

	/* Set attr */
	dp->attr = ARCHIVE;
	if ((sb->st_mode&S_IFMT) == S_IFDIR)
		dp->attr |= DIRECT;	/* It's a directory */

	for (p = dp->fill; p < dp->fill+10; *p++ = 0)
		;

	/* Set time */
	tm = localtime(&sb->st_mtime);
	dp->hour = tm->tm_hour;
	dp->minute = tm->tm_min;
	dp->second = tm->tm_sec;
	dp->year = tm->tm_year-80;
	dp->month = tm->tm_mon+1;
	dp->day = tm->tm_mday;

	dp->start = 0;		/* Starting cluster */
	if (dp->attr&DIRECT)
		dp->size = 0;
	else
		dp->size = sb->st_size;
}

/*
 *	Extract named file from the disk
 */
void
extract(f)
char	*f;
{
	register i, start;
	char	*namepart = f;
	char	*end;
	int	num = NDIR;
	dir	*dp = rootdir;
	dir	*dirp = rootdir;

 again:
	end = strchr(namepart,'/');
	if (end != NULL)
		*end = '\0';
	/*
	 *	Search for the file/subdirectory 'namepart'
	 */
	for (i = 0; i < num; i++, dp++)
	{
		if (strcmp(namepart,fixname(dp->name)))
			continue;
		/*
		 *	Found current part of pathname
		 */
		if (dp->attr&DIRECT)
		{
			/*
			 *	Load in the directory
			 */
			start = dp->start;
			if (dirp != rootdir)
				free(dirp);
			dp = getdir(start);
			num = getdir_num;
			if (end == NULL)
			{		/* Extract whole directory */
				extrall(f,dp,num);
				return;
			}
			*end = '/';	/* Restore the / */
			namepart = end+1;
			goto again;
		}
		/* It's a file */
		if (end != NULL)
		{
			/* ... but we expect a directory */
			printf("%s is not a directory\n",f);
			*end = '/';
		}
		else
			do_extract(f,dp);
		return;
	}
	/*
	 *	Search failed
	 */
	printf("%s doesn't exist\n",f);
}

/*
 *	Given the pathname of a directory and
 *	the actual contents of the directory,
 *	recursively extract every file and directory from it
 */
void
extrall(prefix,dp,num)
char	*prefix;
dir	*dp;
{
	register i;
	char	newprefix[130];
	dir	*sub;

	for (i = 0; i < num; i++, dp++)
	{
		if (dp->name[0] == 0)
			break;
		if (dp->name[0] == (char)0xE5)
			continue;
		if (dp->attr&VOLUME)
			continue;

		newprefix[0] = '\0';
		if (prefix[0] != '\0')
		{
			strcpy(newprefix,prefix);
			strcat(newprefix,"/");
		}
		strcat(newprefix,fixname(dp->name));
		if (dp->attr&DIRECT)
		{
			if (dp->name[0] != '.')
			{
				show('x',newprefix);
				sub = getdir(dp->start);
				extrall(newprefix,sub,getdir_num);
				free(sub);
			}
		}
		else
			do_extract(newprefix,dp);
	}
}

/*
 *	Given an MSDOS directory structure and a UNIX pathname,
 *	make all directories leading to the file,
 *	create the file and
 *	copy the MSDOS file to the UNIX file,
 *	taking care of any conversions.
 */
void
do_extract(unixname,dp)
char	*unixname;
dir	*dp;
{
	long	addr;
	char	*buf;
	char	*buf1;
	int	fd;
	int	clus;
	int	mode;
	int	r;
	char	*p, *q;
	/*		This is here for when we restore mod times to UNIX
	time_t	tb[2];
	 */

	if (dp->attr&RONLY)
		mode = 0444;
	else
		mode = 0666;
	fd = makefile(unixname,mode);
	if (fd < 0)
		return;

	buf = Malloc(CLUSIZE);
	buf1 = Malloc(CLUSIZE);
	for (
		clus = dp->start, addr = 0;
		addr < dp->size;
		addr += CLUSIZE, clus = getfat(clus)
	)
	{
		readclus(clus,buf);
		r = CLUSIZE;
		if (addr+CLUSIZE > dp->size)	/* Less than 1 block left */
			/* Don't worry if lint complains about this */
			r = (int)(dp->size-addr);
		if (!binary) {
			/*
			 *	Do cr-nl mapping
			 */
			for (p = buf, q = buf1; p < buf+r; p++)
			{
				if (*p == '\r')
					*q++ = '\n';
				else if (*p == '\032')
					break;	/* ^Z is end of file char */
				else if (*p != '\n')
					*q++ = *p;
			}

			if (write(fd,buf1,q-buf1) != q-buf1) {
				printf("Write error on %s\n",unixname);
				break;
			}
		} else if (write(fd,buf,r) != r) {
			printf("Write error on %s\n",unixname);
			break;
		}
	}
	close(fd);
	free(buf);
	free(buf1);
	show('x', unixname);

	/*
	 *	No code to calculate mtime yet!
	 *	Sorry ...
	 *
	tb[0] = mtime;
	tb[1] = mtime;
	utime(unixname,tb);	* Set modified time */
}

makefile(name,mode)
char	*name;
{
	register char	*p = name;
	register of;
	struct	stat	stbuf;
	char	*strchr();

	while ((p = strchr(p,'/')) != NULL) {
		*p = '\0';
		if (stat(name,&stbuf) < 0	/* dir doesn't exist */
		 || mkdir(name, 0777))
		{
			fprintf(stderr, "Unable to make directory %s\n", name);
			return -1;
		}
		else if ((stbuf.st_mode&S_IFMT) != S_IFDIR) {
			fprintf(stderr,"File in path: %s\n",name);
			return -1;
		}
		*p++ = '/';
	}
	of = creat(name,mode);
	if (of < 0)
		perror(name);
	return of;
}

/*
mkdir(n, mode)
char	*n;
{
	char	buf[120];

	strcpy(buf,"mkdir ");
	strcat(buf,n);
	return system(buf);
}
*/

void
delete(f)
char	*f;
{
	register start = 0;
	char	*namepart = f;
	char	*end;
	int	num = NDIR;
	dir	*dp = rootdir;
	dir	*dirp = rootdir;

 again:
	end = strchr(namepart,'/');
	if (end != NULL)
		*end = '\0';
	/*
	 *	Search for the file/subdirectory 'namepart'
	 */
	for (; dp < dirp+num; dp++)
	{
		if (strcmp(namepart,fixname(dp->name)))
			continue;
		/*
		 *	Found current part of pathname
		 */
		if (dp->attr&DIRECT)
		{
			int	pnum = num, pclus = start;
			dir	*parent = dirp, *entry = dp;
			/*
			 *	Load in the directory
			 */
			start = dp->start;
			if (end != NULL && dirp != rootdir)
				free(dirp);
			dirp = dp = getdir(start);
			num = getdir_num;
			if (end == NULL)
			{		/* Delete whole directory */
				/*
				 *	Check that directory is empty
				 *	(apart from "." and "..")
				 */
				for (;dp < dirp+num; dp++)
				{
					if (dp->name[0] == 0)
						break;	/* Never used */
					if (dp->name[0] != '.'
					 && dp->name[0] != (char)0xE5)
					{
						printf("%s: Directory not empty\n",f);
						free(dirp);
						if (parent != rootdir)
							free(parent);
						return;
					}
				}
				show('d',f);
				/* Free the directory's blocks */
				truncate(start);
				free(dirp);
				/* Delete entry from parent */
				entry->name[0] = 0xE5;
				putdir(pclus,parent,pnum);
				if (parent != rootdir)
					free(parent);
				return;
			}
			*end = '/';	/* Restore the / */
			namepart = end+1;
			goto again;
		}
		/* It's a file */
		if (end != NULL)
		{
			/* ... but we expect a directory */
			printf("%s is not a directory\n",f);
			*end = '/';
			return;
		}
		show('d',f);
		truncate(dp->start);	/* Free the files clusters */
		dp->name[0] = 0xE5;	/* Delete the file */
		putdir(start,dirp,num);	/* Rewrite the directory */
		if (dirp != rootdir)
			free(dirp);
		return;
	}
	/*
	 *	Search failed
	 */
	printf("%s doesn't exist\n",f);
}

/*
 *	Given the pathname of a directory and
 *	the actual contents of the directory,
 *	list out either:
 *		the contents of the directory and it's subdirectories
 *		The named files and/or directories
 */
void
listdir(prefix,direct, num)
char	*prefix;
dir	*direct;
{
	register i, j;
	register dir	*dp = direct;
	register dir	*sub;
	char	fullname[130];
	static	struct
	{
		char	bit;
		char	flag;
	}	flags[] =
	{
		{ DIRECT, 'd' },
		{ RONLY, 'r' },
		{ HIDDEN, 'h' },
		{ SYSTEM, 's' },
		{ 0,	0 },
		{ ARCHIVE, 'a' }
	};

	for (j = 0; j < num; j++, dp++)
	{
		if (dp->name[0] == 0)
			break;
		if (dp->name[0] == (char)0xE5)
			continue;
		if (dp->attr&VOLUME)
			continue;
		fullname[0] = '\0';
		if (prefix[0] != '\0')
		{
			strcpy(fullname,prefix);
			strcat(fullname,"/");
		}
		strcat(fullname,fixname(dp->name));
		if (ismatch(fullname) != 2)
			continue;
		/* Don't show directory if we'll show contents */
		if (nfiles && dp->attr&DIRECT)
			continue;
		/*
		 *	Don't list . and ..
		 */
		if (dp->name[0] == '.')
			continue;

		if (verbose)
		{
			for (i = 0; flags[i].bit; i++)
				if (dp->attr & flags[i].bit)
					putchar(flags[i].flag);
				else
					putchar('-');

			printf(" %02d:%02d:%02d %02d/%02d/%02d ",
				dp->hour,dp->minute,dp->second*2,
				dp->day,dp->month,1980+dp->year);

			if (dp->attr&DIRECT)
				printf("        ");
			else
				printf("%8ld ",dp->size);
		}
		else if (*prefix != '\0')
			printf("%s/",prefix);

		printf("%s\n",fixname(dp->name));
	}
	/*
	 *	List out subdirectories
	 */
	for (dp = direct, j = 0; j < num; j++, dp++)
	{
		if (dp->name[0] == 0)
			break;
		if (dp->name[0] == (char)0xE5)
			continue;
		if ((dp->attr&DIRECT) == 0)
			continue;
		/* Don't go recursive on . and .. ! */
		if (dp->name[0] == '.')
			continue;
		fullname[0] = '\0';
		if (prefix[0] != '\0')
		{
			strcpy(fullname,prefix);
			strcat(fullname,"/");
		}
		strcat(fullname,fixname(dp->name));
		i = ismatch(fullname);
		if (i == 0)
			continue;
		if (verbose)
			printf("\n%s:\n",fullname);
		sub = getdir(dp->start);
		listdir(fullname,sub,getdir_num);
		free(sub);
	}
}

/*
 *	Given the starting cluster of a directory,
 *	read the directory into Malloc'ed space and return it,
 *	setting getdir_num to the maximum number of entries.
 */
dir *
getdir(start)
{
	dir	*sub;
	int	count, clus;

	/*
	 *	Read the directory first to find out how big it is
	 */
	count = 1;
	clus = start;
	while ((clus = getfat(clus)) < 0xFF7 && clus)
		count++;
	/* Allocate memory */
printf("Dir is %d clusters starting at %d\n", count, start);
	/* Enough for one extra entry is allocated */
	sub = (dir *)Malloc(count*CLUSIZE + sizeof(dir));
	sub[count*DPCLUS].name[0] = 0;

	clus = start;
	count = 0;		/* Count clusters */
	do if (!readclus(clus,sub + count*CLUSIZE))
	{
		fprintf(stderr,"Directory read error may cause headaches\n");
		/* Use whatever we can of the directory */
		break;
	}
	else
		count++;
	while ((clus = getfat(clus)) < 0xFF7 && clus);

	/* hex_dump(sub, count*CLUSIZE); */

	fixdir(sub,getdir_num = count*CLUSIZE/sizeof(dir));
	return sub;
}

/*
 *	Rewrite the directory
 *	This compresses the directory first, freeing space if possible.
 *	Note: in a "replace", the directory may have grown by one entry.
 *	We will need to allocate another cluster if it overflows.
 */
void
putdir(start,dp,num)
dir	*dp;
{
	register i, realnum, count;
	register dir	*dfrom, *dto;
	register next, last;

	/*
	 *	Crush out deleted entries
	 */
	for (dfrom = dto = dp, i = 0; i < num; i++, dfrom++)
	{
		if (dfrom->name[0] == 0)
			break;	/* Never used */
		if (dfrom->name[0] == (char)0xE5)
		{
			dfrom->name[0] = 0;
			continue;
		}
		if (dfrom != dto)
		{
			*dto = *dfrom;
			dfrom->name[0] = 0;
		}
		dto++;
	}

	if (dp == rootdir)
	{		/* Just say root dir must be written */
		root_mod = 1;
		return;
	}

	realnum = dto-dp;
	fixdir(dp,num);
	next = start;
	for (count = 0; count < realnum; count += DPCLUS)
	{
		if (next <= 0 || next >= 0xFF7)
		{		/* No cluster allocated - dir grew */
			last = next;
			next = getfree();
			if (last == 0)
			{		/* Can't happen ... */
				/* dir had no clusters */
				start = next;
			}
			else
				putfat(last,next);
			putfat(next,0xFF8);
		}
		if (!writeclus(next,(char *)(dp+count)))
		{
			fprintf(stderr,"Directory write error - scrambled eggs !\n");
			break;
		}
		last = next;
		next = getfat(next);
	}
	if (next >= 2 && next < 0xFF7)
	{		/* directory got shorter */
		putfat(last,0xFF8);
		truncate(next);	/* Free remaining blocks */
	}
}

/*
 *	Do all the byte swapping etc for a directory so we can look at it.
 *	NOTE:	This process works both ways.
 */
dir *
fixdir(dp,num)
dir	*dp;
{
	char	*from, *to;
	short	tmp;
	register i;
	dir	*vol = NULL;

	for (i = 0; i < num; i++, dp++)
	{
		if (dp->name[0] == 0)
			break;
		if (dp->name[0] == (char)0xE5)
			continue;
#ifndef	NOSWAB
		/*
		 *	Do byte swapping for numeric data
		 */
		from = (char *)&dp->SWABFROM;
		to = (char *)&dp->SWABTO+sizeof(dp->SWABTO);
		myswab(from, to-from);

		tmp = dp->s.ssize[0];
		dp->s.ssize[0] = dp->s.ssize[1];
		dp->s.ssize[1] = tmp;
#endif // NOSWAB

		if (dp->attr&VOLUME)
			vol = dp;
	}
	return vol;
}

/*
 *	Free the chain of clusters beginning with "start"
 */
void
truncate(start)
{
	register next;

	while (start > 0 && start < 0xFF7)
	{
		next = getfat(start);
		putfat(start,0);
		start = next;
	}
}

/*
 *	Unpack the i'th entry from the file allocation table.
 */
getfat(i)
{
	int	num;

	if (i < 2 || i >= NCLUS)
		return -1;
	num = i*3/2;
	if (i&01)
		return ((fat[num]>>4)&0xF) | ((fat[num+1]<<4)&0xFF0);
	else
		return (fat[num]&0xFF) | ((fat[num+1]<<8)&0xF00);
}

/*
 *	Pack a value into the i'th entry of the fat
 */
void
putfat(i,val)
{
	int	num;

	if (i < 2 || i >= NCLUS)
		return;
	num = i*3/2;
	if (i&01)
	{
		fat[num] = (fat[num]&0xF) | ((val<<4)&0xF0);
		fat[num+1] = (val>>4)&0xFF;
	}
	else
	{
		fat[num] = val&0xFF;
		fat[num+1] = (fat[num+1]&0xF0) | ((val>>8)&0xF);
	}
	fat_mod = 1;
}

/*
 *	Find a free cluster
 */
getfree()
{
	int	i;

	for (i = 2; i < NCLUS; i++)
		if (getfat(i) == 0)
			return i;
	return 0;	/* None free */
}

/*
 *	Return free space on disk in bytes
 */
long
diskfree()
{
	long	space = 0;
	int	i;

	for (i = 2; i < NCLUS; i++)
		if (getfat(i) == 0)
			space++;
	return space*CLUSIZE;
}

/*
 *	Read in the file allocation table (fat)
 *	and the root directory
 */
dir *
getdisk()
{
	long	fatno;

	/*
	 *	Read the boot record.
	 */
	readboot();

	rootdir = (dir *)Malloc(sizeof(dir)*NDIR);
	fat = Malloc(FATSIZE);

	/* Get fat */
	for (
	    fatno = 0;
	    fatno < NFAT
	 && lseek(disk,FAT1 + fatno*FATSIZE,0) != -1
	 && read(disk,fat,FATSIZE) != FATSIZE;	/* Try again if read error */
	    fatno++
	)
		;

	if (cmd != 't' && fatno == NFAT)
		erexit("Can't read file allocation table\n", 0);

	/* seek to start of directory */
	lseek(disk,rootaddr = FAT1 + NFAT*FATSIZE,0);
	if (read(disk,rootdir,sizeof(dir)*NDIR) != sizeof(dir)*NDIR)
		erexit("Read error on root directory\n", 0);
	database = lseek(disk,0L,1);
	return fixdir(rootdir,NDIR);
}

/*
 *	Initialize the fat, and the root directory
 */
void
dos_format()
{
	register char	*cp;
	register nclus, i;
	int	rds;
	dir	*dp;

	/*
	 *	Write the boot record.
	 */
	writeboot();

	/*
	 *	Create empty root directory
	 */
	rds = sizeof(dir)*NDIR;
	rootaddr = FAT1+NFAT*FATSIZE;
	database = rootaddr+rds;
	rootdir = (dir *)Malloc(rds);
	cp = (char *)rootdir;
	while (cp < (char *)rootdir+rds)
		*cp++ = 0xE5;

	for (dp = rootdir; dp < rootdir+NDIR; dp++)
		dp->name[0] = 0;
	root_mod = 1;

	/*
	 *	Initialize fat
	 */
	fat = Malloc(FATSIZE);
	fat[0] = fat[1] = fat[2] = 0xFF;	/* Media type bytes */

	nclus = NCLUS;
	NCLUS = FATSIZE*2/3;
	for (i = nclus; i < NCLUS; i++)
		putfat(i,0xFF9);		/* Fill end of fat */
	NCLUS = nclus;

	for (i = 2; i < NCLUS; i++)
		putfat(i,0);			/* Mark it free */
}

typedef	unsigned char	uchar;

struct	boot
{
	uchar	jump[3];	/* 0x EB 1C 90	jump to boot code. */
	uchar	oem[8];		/* machine name, e.g. "HP150   " */
	uchar	bps[2];		/* Bytes per sector */
	uchar	spc;		/* sectors per cluster */
	uchar	rs[2];		/* reserved sectors */
	uchar	cf;		/* copies of FAT */
	uchar	mde[2];		/* maximum dir entries */
	uchar	ts[2];		/* total sectors */
	uchar	md;		/* media descriptor */
	uchar	sf[2];		/* sectors in FAT */
	uchar	st[2];		/* sectors per track */
	uchar	nh[2];		/* number of heads */
	uchar	hs[2];		/* hidden sectors */
}
	boot =
{
	"\353\34\220",
	"HP150   ",
	0			/* Other crud */
};

/*
 *	Read the boot record.
 */
void
readboot()
{
	lseek(disk,0L,0);
	read(disk, &boot, sizeof(boot));

	showboot(&boot);
}

#define	two(p)	((p[1]<<8) + p[0])

void
showboot(b)
struct	boot	*b;
{
			/* EB 1C 90 jump to boot code. */
	printf("jump	%02X %02X %02X\n", b->jump[0], b->jump[1], b->jump[2]);
	printf("oem	'%.8s' - machine name\n", b->oem);
	printf("bps	%d - bytes per sector\n", two(b->bps));	
	printf("spc	%d - sectors per cluster\n", b->spc);		
	printf("rs	%d - reserved sectors\n", two(b->rs));	
	printf("cf	%d - copies of FAT\n", b->cf);		
	printf("mde	%d - maximum dir entries\n", two(b->mde));	
	printf("ts	%d - total sectors\n", two(b->ts));	
	printf("md	%d - media descriptor\n", b->md);		
	printf("sf	%d - sectors in FAT\n", two(b->sf));	
	printf("st	%d - sectors per track\n", two(b->st));	
	printf("nh	%d - number of heads\n", two(b->nh));	
	printf("hs	%d - hidden sectors\n", two(b->hs));	
}

/*
 *	Write the boot record.
 */
void
writeboot()
{
	lseek(disk,0L,0);

	/*
	 *	Fill in boot record.
	 */
	write(disk, &boot, sizeof(boot));

	/*
	 *	Write 0xFF at start of second sector. ... Why?
	 */
	lseek(disk,(long)SECSIZE,0);
	write(disk,"\377",1);
}

/*
 *	Rewrite the fat and/or the root directory after modifying the disk
 */
void
dos_end()
{
	int	fatno;

	if (root_mod)
	{
		fixdir(rootdir,NDIR);
		lseek(disk,rootaddr,0);
		if (write(disk,rootdir,sizeof(dir)*NDIR) != sizeof(dir)*NDIR)
		    erexit("Write error on root directory - scrambled eggs\n", 0);
	}
	if (fat_mod)
	{		/* Write the fat the required no of times */
		for (fatno = 0; fatno < NFAT; fatno++)
		{
			lseek(disk,FAT1 + fatno*FATSIZE,0);
			if (write(disk,fat,FATSIZE) != FATSIZE)
				printf("Write error on FAT copy %d ignored\n",fatno);
		}
	}
}

/*
 *	Turn an MSDOS format name into a UNIX format name
 */
char * 
fixname(dosname)
char	*dosname;
{
	register	k;
	static	char	buf[14];
	register char	*cp = buf;

	for (k = 0; k < 8 && dosname[k] != ' '; k++)
		*cp++ = lcase(dosname[k]);
	*cp = '\0';
	if (dosname[8] == ' ')
		return buf;
	*cp++ = '.';
	for (k = 8; k < 11 && dosname[k] != ' '; k++)
		*cp++ = lcase(dosname[k]);
	*cp = '\0';
	return buf;
}

/*
 *	Read a cluster
 */
readclus(clus,data)
char	*data;
{
	register char	*dp;

	lseek(disk, (clus-2)*CLUSIZE + database,0);
	if (read(disk,data,CLUSIZE) != CLUSIZE)
	{
		fprintf(stderr,"Read error on cluster %d ignored\n",clus);
		for (dp = data; dp < data+CLUSIZE; dp++)
			*dp = 0;
		return 0;
	}
	return 1;
}

/*
 *	Write a cluster
 */
writeclus(clus,data)
char	*data;
{
	lseek(disk, (clus-2)*CLUSIZE + database,0);
	if (write(disk,data,CLUSIZE) != CLUSIZE)
	{
		fprintf(stderr,"Write error on cluster %d\n",clus);
		return 0;
	}
	return 1;
}

/*
 *	Return 2 if any of the files in "files" is a prefix of "name"
 *	return 1 if "name" is a prefix of any of the files in "files"
 */
ismatch(name)
char	*name;
{
	int	i;

	if (nfiles == 0)
		return 2;
	for (i = 0; i < nfiles; i++)
	{
		if (strncmp(name,files[i],strlen(files[i])) == 0)
			return 2;	/* files[i] is a prefix */
		if (strncmp(name,files[i],strlen(name)) == 0)
			return 1;	/* name is a prefix */
	}
	return 0;
}

/*
 *	Byte swapping routine
 *	swap bytes in cp for len
 */
void
myswab(cp,len)
char	*cp;
{
	char	c;

	while (len >= 2)
	{
		c = *cp;
		*cp = cp[1];
		cp[1] = c;
		len -= 2;
		cp += 2;
	}
}

/*
 *	malloc with checking.
 */
char *
Malloc(bytes)
{
	register char *cp = malloc(bytes);

	if (!cp)
	{
		printf("Help! Out of memory... aborting\n");
		exit(1);
	}
	return cp;
}

void
hex_dump(data, length)
unsigned char	*data;
{
	register i, j;

	printf("%d bytes\n", length);

	for (i = 0; i < length; i += 20)
	{
		for (j = 0; j < 20;)
		{
			if (i+j < length)
				printf("%02x", data[j]);
			else
				printf("  ");
			if ((++j & 03) == 0)
				printf(" ");
		}

		printf("\t|");

		for (j = 0; j < 20; j++)
			if (i+j >= length)
				printf(" ");
			else if (data[j] >= ' ' && data[j] <= '~')
				printf("%c", data[j]);
			else
				printf(".");
		printf("|\n");
		data += 20;
	}
	if (i % 20)
		printf("\n");
}
