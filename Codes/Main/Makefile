objects1 := computing_func comunication_func linear_reg
objects2 := process_stage
objects3 := Main

#Pasar nombre a level -- Los objects/headers/source añadir sufijo

DISTRI_LOC = $(TOP)/malleability/distribution_methods/block_distribution.h
MALLEABILITY_DEPENDS = $(TOP)/malleability/CommDist.h $(TOP)/malleability/malleabilityStates.h $(TOP)/malleability/malleabilityManager.h
DEPENDS := Main_datatypes.h
CC := gcc
MCC := mpicc
CFLAGS := -Wall -Wextra

all: $(objects1) $(objects2) $(objects3)

$(objects1): %: %.c %.h $(DEPENDS)
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<
$(objects2): %: %.c %.h $(objects1).h $(DEPENDS) $(DISTRI_LOC)
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<
$(objects3): %: %.c $(objects2).h $(DEPENDS) $(TOP)/IOcodes/read_ini.h $(TOP)/IOcodes/results.h $(MALLEABILITY_DEPENDS)
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<

