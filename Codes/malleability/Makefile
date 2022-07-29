dir_targets := distribution_methods
objects1 := CommDist
objects2 := malleabilityTypes ProcessDist
objects3 := malleabilityZombies
objects4 := malleabilityManager
DEPENDS := $(addsuffix .h, malleabilityDataStructures malleabilityStates)
CC := gcc
MCC := mpicc
CFLAGS := -Wall -Wextra

.PHONY: $(dir_targets) subdir

all: subdir $(objects1) $(objects2) $(objects3) $(objects4)

subdir: $(dir_targets)
$(dir_targets): %:
	$(MAKE) -C $@

$(objects1): %: %.c %.h $(DEPENDS) $(dir_targets)/block_distribution.h
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<

$(objects2): %: %.c %.h $(DEPENDS)
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<

$(objects3): %: %.c %.h $(DEPENDS) $(TOP)/IOcodes/results.h
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<

$(objects4): %: %.c %.h $(objects1).h $(objects2).h $(objects3).h $(DEPENDS) \
	                $(TOP)/IOcodes/read_ini.h $(TOP)/IOcodes/results.h $(TOP)/Main/Main_datatypes.h
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<
