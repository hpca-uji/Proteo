CC = gcc
MCC = mpicc
C_FLAGS_ALL = -Wfatal-errors -Wall -Wextra -Wpedantic -Wconversion -Wshadow
C_FLAGS = -Wall
LD_FLAGS = -lm -lslurm -pthread

.PHONY : clean clear

# Final binary
BIN = a.out
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

# Default target named after the binary.
$(BIN) : $(BUILD_DIR)/$(BIN)

# Actual target of the binary - depends on all .o files.
$(BUILD_DIR)/$(BIN) : $(OBJ)
	$(MCC) $(C_FLAGS_ALL) $^ -o $@ $(LD_FLAGS)

# Include all .d files
# .d files are used for knowing the dependencies of each source file
-include $(DEP)

# Build target for every single object file.
# The potential dependency on header files is covered
# by calling `-include $(DEP)`.
# The -MMD flags additionaly creates a .d file with
# the same name as the .o file.
$(BUILD_DIR)/%.o : %.c
	mkdir -p $(@D)
	$(MCC) $(C_FLAGS_ALL) -MMD -c $< -o $@

clean:
	-rm $(BUILD_DIR)/$(BIN) $(OBJ) $(DEP)
clear:
	-rm -rf $(BUILDDIR)

install: 

