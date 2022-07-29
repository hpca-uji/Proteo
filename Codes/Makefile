export TOP := $(dir $(CURDIR)/$(word $(words $(MAKEFILE_LIST)),$(MAKEFILE_LIST)))
BUILD := build
EXEC := exec

EXECDIR := $(addprefix $(TOP),$(EXEC))
export BUILDDIR = $(addprefix $(TOP),$(BUILD))
SUBDIRS := IOcodes Main malleability

.PHONY: subdirs $(SUBDIRS) build all clean clear
#
#
#
#
CC := gcc
MCC := mpicc
CFLAGS := -Wall -Wextra
LIBFLAGS := -lm -lslurm -pthread
#
#
#
#

all: subdirs exec
install: 
	#runTests

exec: subdirs
	mkdir -p $(EXECDIR) all
	$(MCC) $(CFLAGS) -o $(EXECDIR)/a.out $(wildcard $(BUILDDIR)/*.o) $(LIBFLAGS)

subdirs: $(SUBDIRS)
$(SUBDIRS): | $(BUILD)
	$(MAKE) -C $@


# Carpeta en la que almacenar los compilados y los ejecutables
$(BUILD):
	mkdir -p $(BUILDDIR)

clean:
	-rm $(BUILDDIR)/*.o
clear:
	-rm -rf $(BUILDDIR)
	-rm -rf $(EXECDIR)

