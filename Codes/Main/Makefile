objects1 := computing_func comunication_func linear_reg
objects2 := process_stage
objects3 := Main
depends := Main_datatypes
CC := gcc
MCC := mpicc
CFLAGS := -Wall

all: $(objects1) $(objects2) $(objects3)

$(objects1): %: %.c %.h $(depends).h
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<
$(objects2): %: %.c %.h $(objects1).h $(depends).h $(TOP)/IOcodes/read_ini.h \
				$(TOP)/malleability/distribution_methods/block_distribution.h
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<
$(objects3): %: %.c $(objects2).h $(depends).h $(TOP)/IOcodes/read_ini.h $(TOP)/IOcodes/results.h \
	                        $(TOP)/malleability/CommDist.h $(TOP)/malleability/malleabilityStates.h \
				$(TOP)/malleability/malleabilityManager.h
	$(MCC) $(CFLAGS) -c -o $(BUILDDIR)/$@.o $<
