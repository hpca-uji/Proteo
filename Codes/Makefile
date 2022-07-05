export TOP := $(dir $(CURDIR)/$(word $(words $(MAKEFILE_LIST)),$(MAKEFILE_LIST)))
BUILD := build
export BUILDDIR = $(addprefix $(TOP),$(BUILD))
SUBDIRS := IOcodes Main malleability

.PHONY: subdirs $(SUBDIRS) build all
#
#
#
#
CC := gcc
MCC := mpicc
CFLAGS := -Wall
LIBFLAGS := -lm -lslurm -pthread
#
#
#
#

all: subdirs exec

exec: subdirs
	$(MCC) $(CFLAGS) -o test.out $(wildcard $(BUILDDIR)/*.o) $(LIBFLAGS)

subdirs: $(SUBDIRS)
$(SUBDIRS): | $(BUILD)
	$(MAKE) -C $@

# Orden de compilacion para las carpetas

# Carpeta en la que almacenar los compilados
$(BUILD):
	mkdir -p $(BUILD)

clean:
	-rm -rf $(BUILD)
