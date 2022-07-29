objects := ini.o read_ini.o results.o
DISTRI_LOC = $(TOP)/malleability/distribution_methods/block_distribution.h
CC := gcc
MCC := mpicc
CFLAGS := -Wall -Wextra

all: $(objects)

ini.o: ini.c ini.h
	echo $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $(BUILDDIR)/$@ $<

read_ini.o: read_ini.c read_ini.h ini.h $(DISTRI_LOC)
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@ $<

results.o: results.c results.h
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@ $<

#$@ --> Objeto
#$< --> Source
