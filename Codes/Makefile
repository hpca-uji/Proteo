CC = gcc
MCC = mpicc
#C_FLAGS_ALL = -Wconversion -Wpedantic
C_FLAGS = -Wall -Wextra -Wshadow -Wfatal-errors
LD_FLAGS = -lm -pthread

MAM_USE_SLURM ?= 0
MAM_USE_BARRIERS ?= 0
MAM_DEBUG ?= 0

ifeq ($(MAM_USE_SLURM),1)
    LD_FLAGS += -lslurm
endif
DEF = -DMAM_USE_SLURM=$(MAM_USE_SLURM) -DMAM_USE_BARRIERS=$(MAM_USE_BARRIERS) -DMAM_DEBUG=$(MAM_DEBUG)

# Final binary
BIN = a.out
CONFIG = config.txt
# Put all auto generated stuff to this build dir.
BUILD_DIR = ./build

# List of all directories where source files are located
SRCDIRS = IOcodes Main malleability malleability/spawn_methods malleability/distribution_methods

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
	-rm $(BUILD_DIR)/$(BIN) $(BUILD_DIR)/$(CONFIG) $(OBJ) $(DEP)
clear:
	-rm -rf $(BUILD_DIR)

install: $(BIN) $(CONFIG)
	echo "Done"

# SPECIFIC RULES
# Default configuration file
$(CONFIG) : $(BUILD_DIR)/$(CONFIG)
# Default target named after the binary.
$(BIN) : $(BUILD_DIR)/$(BIN)

$(BUILD_DIR)/$(CONFIG) :
	@mkdir -p $(@D)
	@ echo -n "dir=\"" > $(BUILD_DIR)/$(CONFIG)
	@ realpath -z $$(echo "$$(pwd)/..") | tr -d '\0' >> $(BUILD_DIR)/$(CONFIG)
	@ echo "\"" >> $(BUILD_DIR)/$(CONFIG)

# Actual target of the binary - depends on all .o files.
$(BUILD_DIR)/$(BIN) : $(OBJ)
	$(MCC) $(C_FLAGS) $^ -o $@ $(LD_FLAGS)

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
	$(MCC) $(C_FLAGS) $(DEF) -MMD -c $< -o $@
