dir_targets := distribution_methods
objects1 := CommDist
objects2 := malleabilityTypes malleabilityZombies ProcessDist
objects3 := malleabilityManager
depends := $(addsuffix .h, malleabilityDataStructures malleabilityStates)
CC := gcc
MCC := mpicc
CFLAGS := -Wall

.PHONY: $(dir_targets) subdir

all: subdir $(objects1) $(objects2) $(objects3)

subdir: $(dir_targets)
$(dir_targets): %:
	$(MAKE) -C $@

$(objects1): %: %.c %.h $(depends) $(dir_targets)/block_distribution.h
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<

$(objects2): %: %.c %.h $(depends) $(TOP)/IOcodes/results.h
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<

$(objects3): %: %.c %.h $(objects1).h $(objects2).h $(depends) \
	                $(TOP)/IOcodes/read_ini.h $(TOP)/IOcodes/results.h
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<

#$(objects1) $(objects2) $(objects3)
# CommDist.c   
# malleabilityTypes.c  malleabilityZombies.c  ProcessDist.c  
#
# malleabilityManager.c
#          malleabilityDataStructures.h               malleabilityStates.h  
