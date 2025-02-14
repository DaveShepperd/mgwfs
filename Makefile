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

OBJS = mgwfsf.o freemap.o

default: mgwfsf

mgwfsf: $(OBJS) Makefile
	$(LD) -o $@ $(OBJS) $(LFLAGS)

mgwfsf.o: mgwfsf.c mgwfsf.h agcfsys.h Makefile
	$(CC) $(CFLAGS) -c $<

freemap_sa.o: freemap.c Makefile
	$(CC) $(SA_CFLAGS) -o $@ -DSTANDALONE_FREEMAP $<

freemap: freemap_sa.o Makefile
	$(CC) $(SA_LFLAGS) -o $@ $<

clean:
	rm -rf Debug Release *.o mgwfsf freemap
