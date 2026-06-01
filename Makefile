DBG = -g
OPT =
STD = -std=gnu11
INCS = -I/usr/include/fuse3 -I.
WARN = -Wall
CFLAGS = $(DBG) $(OPT) $(STD) $(INCS) $(WARN)
SA_CFLAGS = -g -c $(STD) $(INCS) $(WARN)
LIBS = -lfuse3 
LFLAGS = $(DBG) $(LIBS)
SA_LFLAGS = -g
CC = gcc
LD = gcc

OBJS = main.o mgwfs.o freemap.o fuse.o
HS = agcfsys.h mgwfs.h mgwfsctl.h

default: mgwfs mgwfsctl

mgwfs: $(OBJS) Makefile
	$(LD) -o $@ $(OBJS) $(LFLAGS)

# Standalone control/query helper. No fuse dependency; only needs the
# shared ioctl ABI header.
mgwfsctl.o: mgwfsctl.c mgwfsctl.h Makefile
	$(CC) -c $(CFLAGS) $<

mgwfsctl: mgwfsctl.o Makefile
	$(LD) $(SA_LFLAGS) -o $@ mgwfsctl.o

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
	rm -rf Debug Release *.o mgwfs mgwfsctl freemap freemap_sa
