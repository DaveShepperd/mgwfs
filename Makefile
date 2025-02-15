FUSE3_DIR=/usr/local

DBG = -g
OPT =
STD = -std=gnu11
INCS = -I$(FUSE3_DIR)/include/fuse3 -I.
WARN = -Wall
CFLAGS = $(DBG) $(OPT) $(STD) $(INCS) $(WARN)
SA_CFLAGS = -c -Wall -ansi -std=c99 -g
LIBS = $(FUSE3_DIR)/lib/fuse3/libfuse3.so
LFLAGS = $(DBG) $(LIBS)
SA_LFLAGS = -g
CC = gcc
LD = gcc

OBJS = main.o mgwfsf.o freemap.o fuse.o
HS = agcfsys.h mgwfsf.h

default: mgwfsf

mgwfsf: $(OBJS) Makefile
	$(LD) -o $@ $(OBJS) $(LFLAGS)

%.o : %.c
	$(CC) -c $(CFLAGS) $<

main.o: main.c $(HS) Makefile
mgwfsf.o: mgwfsf.c $(HS) Makefile
fuse.o: fuse.c $(HS) Makefile

freemap_sa.o: freemap.c Makefile
	$(CC) $(SA_CFLAGS) -o $@ -DSTANDALONE_FREEMAP $<

freemap: freemap_sa.o Makefile
	$(CC) $(SA_LFLAGS) -o $@ $<

clean:
	rm -rf Debug Release *.o mgwfsf freemap
