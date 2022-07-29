objects1 := Baseline
objects2 := Merge ProcessDist
objects3 := GenericSpawn
CC := gcc
MCC := mpicc
CFLAGS := -Wall -Wextra

all: $(objects1) $(objects2) $(objects3)

$(objects1): %: %.c 
	$(MCC) $(CFLAGS) -c -o $@.o $<
$(objects2): %: %.c $(objects1).o
	echo $@
	$(MCC) $(CFLAGS) -c -o $@.o $<
$(objects3): %: %.c $(objects2).o
	echo $@
	$(MCC) $(CFLAGS) -c -o $@.o $<
clean:
	rm *.o
