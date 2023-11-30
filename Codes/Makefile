CC = gcc
MCC = mpicc
#C_FLAGS_ALL = -Wconversion -Wpedantic
C_FLAGS = -Wall -Wextra -Wshadow -Wfatal-errors
LD_FLAGS = -lm -pthread
DEF =


.PHONY : clean clear install install_slurm

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
	$(MCC) $(C_FLAGS) $^ -o $@ $(LD_FLAGS)
	@ echo -n "dir=\"" > $(BUILD_DIR)/config.txt
	@ realpath -z $$(echo "$$(pwd)/..") >> $(BUILD_DIR)/config.txt
	@ echo "\"" >> $(BUILD_DIR)/config.txt

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
	$(MCC) $(C_FLAGS) $(DEF) -MMD -c $< -o $@

clean:
	-rm $(BUILD_DIR)/$(BIN) $(BUILD_DIR)/config.txt $(OBJ) $(DEP)
clear:
	-rm -rf $(BUILDDIR)


install: $(BIN)
	echo "Done"

# Builds target with slurm
install_slurm: LD_FLAGS += -lslurm
install_slurm: DEF += -DUSE_SLURM
install_slurm: install
