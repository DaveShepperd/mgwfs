DBG = -g
OPT =
STD = -std=gnu11
INCS = -I/usr/include/fuse3 -I.
WARN = -Wall
CFLAGS = $(DBG) $(OPT) $(STD) $(INCS) $(WARN)
SA_CFLAGS = -c -Wall -ansi -std=c99 -g
LIBS = -lfuse3 #$(FUSE3_DIR)/lib/fuse3/libfuse3.so
LFLAGS = $(DBG) $(LIBS)
SA_LFLAGS = -g
CC = gcc
LD = gcc

OBJS = main.o mgwfs.o freemap.o fuse.o
HS = agcfsys.h mgwfs.h

default: mgwfs

mgwfs: $(OBJS) Makefile
	$(LD) -o $@ $(OBJS) $(LFLAGS)

%.o : %.c
	$(CC) -c $(CFLAGS) $<

main.o: main.c $(HS) Makefile
mgwfs.o: mgwfs.c $(HS) Makefile
fuse.o: fuse.c $(HS) Makefile

freemap_sa.o: freemap.c Makefile
	$(CC) $(SA_CFLAGS) -o $@ -DSTANDALONE_FREEMAP $<

freemap: freemap_sa.o Makefile
	$(CC) $(SA_LFLAGS) -o $@ $<

clean:
	rm -rf Debug Release *.o mgwfs freemap
