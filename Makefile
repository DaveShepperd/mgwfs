obj-m := mgwfs.o
mgwfs-objs := kmgwfs.o super.o inode.o dir.o file.o
CFLAGS_kmgwfs.o := -DDEBUG
CFLAGS_super.o := -DDEBUG
CFLAGS_inode.o := -DDEBUG
CFLAGS_dir.o := -DDEBUG
CFLAGS_file.o := -DDEBUG
#CFLAGS_mkfs-mgwfs.o := -Wall -ansi -std=c99
#CFLAGS_dumpfs-mgwfs.o := -Wall -ansi -std=c99

SA_CFLAGS = -c -Wall -ansi -std=c99 -g
SA_LFLAGS = -g

all: ko mkfs-mgwfs dmpfs-mgwfs

ko:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

mkfs-mgwfs.o: mkfs-mgwfs.c mgwfs.h Makefile
	gcc $(SA_CFLAGS) $<

mkfs-mgwfs: mkfs-mgwfs.o Makefile
	gcc $(SA_LFLAGS) -o $@ $<

dmpfs-mgwfs.o: dmpfs-mgwfs.c mgwfs.h Makefile
	gcc $(SA_CFLAGS) $<

dmpfs-mgwfs: dmpfs-mgwfs.o Makefile
	gcc $(SA_LFLAGS) -o $@ $<

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f *.o mkfs-mgwfs dmpfs-mgwfs
