objects1 := ProcessDist
objects2 := Spawn_state
objects3 := Baseline
objects4 := Merge
objects5 := GenericSpawn
CC := gcc
MCC := mpicc
CFLAGS := -Wall

all: $(objects1) $(objects2) $(objects3) $(objects4) $(objects5)

$(objects1): %: %.c %.h
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<

$(objects2): %: %.c %.h
	$(CC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<

$(objects3): %: %.c %.h $(objects2).h
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<

$(objects4): %: %.c %.h $(objects3).h
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<

$(objects5): %: %.c %.h $(objects1).h $(objects2).h $(objects3).h $(objects4).h
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<
