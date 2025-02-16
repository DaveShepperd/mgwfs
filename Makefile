obj-m := mgwfs.o
mgwfs-objs := kmgwfs.o super.o inode.o dir.o file.o freemap.o
CFLAGS_kmgwfs.o := -DDEBUG -g
CFLAGS_super.o := -DDEBUG -g
CFLAGS_inode.o := -DDEBUG -g
CFLAGS_dir.o := -DDEBUG -g
CFLAGS_file.o := -DDEBUG -g

SA_CFLAGS = -c -Wall -ansi -std=c99 -g
SA_LFLAGS = -g

all: ko dmpfs-mgwfs freemap #mkfs-mgwfs 

ko:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

#mkfs-mgwfs.o: mkfs-mgwfs.c mgwfs.h Makefile
#	gcc $(SA_CFLAGS) $<

#mkfs-mgwfs: mkfs-mgwfs.o Makefile
#	gcc $(SA_LFLAGS) -o $@ $<

dmpfs-mgwfs.o: dmpfs-mgwfs.c mgwfs.h Makefile
	gcc $(SA_CFLAGS) $<

freemap_lib.o: freemap.c Makefile
	gcc $(SA_CFLAGS) -o $@ -DSTANDALONE_FREEMAP_LIB $<

dmpfs-mgwfs: dmpfs-mgwfs.o freemap_lib.o Makefile
	gcc $(SA_LFLAGS) -o $@ dmpfs-mgwfs.o freemap_lib.o

freemap_sa.o: freemap.c Makefile
	gcc $(SA_CFLAGS) -o $@ -DSTANDALONE_FREEMAP $<

freemap: freemap_sa.o Makefile
	gcc $(SA_LFLAGS) -o $@ $<

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f *.o mkfs-mgwfs dmpfs-mgwfs freemap
