CC = gcc
MCC = mpicc
#C_FLAGS_ALL = -Wconversion -Wpedantic
C_FLAGS = -Wall -Wextra -Wshadow -Wfatal-errors
LD_FLAGS = -lm -pthread -lmam

MAM_HOME = ../MaM
MAM_FLAGS = -I$(MAM_HOME) -L$(MAM_HOME)/build

ifeq ($(shell test $(MAM_DEBUG) -gt 0; echo $$?),0)
    C_FLAGS += -g -O1
	#C_FLAGS += -fsanitize=address
endif

# Final binary
BIN = a.out
# Put all auto generated stuff to this build dir.
BUILD_DIR = ./build

# List of all directories where source files are located
SRCDIRS = IOcodes Main

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
# Default target named after the binary.
$(BIN) : $(BUILD_DIR)/$(BIN)

# Actual target of the binary - depends on all .o files.
$(BUILD_DIR)/$(BIN) : $(OBJ)
	$(MCC) $(C_FLAGS) $(MAM_FLAGS) $^ -o $@ $(LD_FLAGS)

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
	$(MCC) $(C_FLAGS) $(MAM_FLAGS) $(DEF) -MMD -c $< -o $@
