CC = gcc
MCC = mpicc
#C_FLAGS_ALL = -Wconversion -Wpedantic
C_FLAGS = -Wall -Wextra -Wshadow -Wfatal-errors
LD_FLAGS = -lm -pthread

MAM_USE_SLURM ?= 1
MAM_USE_BARRIERS ?= 0
MAM_DEBUG ?= 0
DEF = -DMAM_USE_SLURM=$(MAM_USE_SLURM) -DMAM_USE_BARRIERS=$(MAM_USE_BARRIERS) -DMAM_DEBUG=$(MAM_DEBUG)

ifdef DMR_PATH
	MPIFLAGS = -I$(MPI_PATH)/include -L$(MPI_PATH)/lib
	SLURMFLAGS = -I$(SLURM_ROOT)/include -L$(SLURM_ROOT)/lib
endif

ifeq ($(MAM_USE_SLURM),1)
    LD_FLAGS += -lslurm
endif

ifeq ($(shell test $(MAM_DEBUG) -gt 0; echo $$?),0)
    C_FLAGS += -g -O1
	#C_FLAGS += -fsanitize=address
endif

# Final library
LIB = libmam.so
# Put all auto generated stuff to this build dir.
BUILD_DIR = ./build

# List of all directories where source files are located
SRCDIRS = . spawn_methods distribution_methods

# List of all .c source files.
C_FILES = $(foreach dire, $(SRCDIRS), $(wildcard $(dire)/*.c))

# All .o files go to build dir.
OBJ = $(C_FILES:%.c=$(BUILD_DIR)/%.o)
# Gcc will create these .d files containing dependencies.
DEP = $(OBJ:%.o=%.d)

# BASIC RULES
.PHONY : clean clear install

all: install

clean:
	-rm $(BUILD_DIR)/$(LIB) $(OBJ) $(DEP)
clear:
	-rm -rf $(BUILD_DIR)

install: $(LIB)
	echo "Done"

# SPECIFIC RULES
# Default target named after the binary.
$(LIB) : $(BUILD_DIR)/$(LIB)

# Actual target of the binary - depends on all .o files.
$(BUILD_DIR)/$(LIB) : $(OBJ)
	$(MCC) $(C_FLAGS) $(MPIFLAGS) $(SLURMFLAGS) $^ -shared -o $@ $(LD_FLAGS)

# Include all .d files
# .d files are used for knowing the dependencies of each source file
-include $(DEP)

# Build target for every single object file.
# The potential dependency on header files is covered
# by calling `-include $(DEP)`.
# The -MMD flags additionaly creates a .d file with
# the same name as the .o file.
$(BUILD_DIR)/%.o : %.c
	@mkdir -p $(@D)
	$(MCC) $(C_FLAGS) $(MPIFLAGS) $(SLURMFLAGS) $(DEF) -fpic -MMD -c $< -o $@
