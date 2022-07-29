objects1 := block_distribution
CC := gcc
MCC := mpicc
CFLAGS := -Wall -Wextra

all: $(objects1)

$(objects1): %: %.c %.h
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<
