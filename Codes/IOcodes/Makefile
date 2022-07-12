objects1 := ini
objects2 := results read_ini
CC := gcc
MCC := mpicc
CFLAGS := -Wall

all: $(objects1) $(objects2)

$(objects1): %: %.c %.h
	$(CC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<
$(objects2): %: %.c %.h $(objects1).h $(TOP)/malleability/distribution_methods/block_distribution.h
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<
