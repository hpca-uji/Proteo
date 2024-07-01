MAM_USE_SLURM ?= 0
MAM_USE_BARRIERS ?= 0
MAM_DEBUG ?= 0

CONFIG = config.txt
PROTEO_HOME := $(shell realpath -z $$(echo "$$(pwd)/..") | tr -d '\0')

# Put all auto generated stuff to this build dir.
BUILD_DIR = ./build

# BASIC RULES
.PHONY : all install sam mam clean sam_clean mam_clean clear sam_clear mam_clear

all: install

# Default configuration file
$(CONFIG) : $(BUILD_DIR)/$(CONFIG)
$(BUILD_DIR)/$(CONFIG):
	@ mkdir -p $(@D)
	@ echo "export PROTEO_HOME=$(PROTEO_HOME)" > $(BUILD_DIR)/$(CONFIG)
	@ echo "export PROTEO_BIN=$(PROTEO_HOME)/Codes/SAM/build/a.out" >> $(BUILD_DIR)/$(CONFIG)
	@ echo "export PATH=\$$PATH:\$$PROTEO_HOME/Codes/MaM" >> $(BUILD_DIR)/$(CONFIG)
	@ echo "export LD_LIBRARY_PATH=\$$LD_LIBRARY_PATH:\$$PROTEO_HOME/Codes/MaM/build" >> $(BUILD_DIR)/$(CONFIG)
	@ echo "codeDir=\"/Codes\"" >> $(BUILD_DIR)/$(CONFIG)
	@ echo "execDir=\"/Exec\"" >> $(BUILD_DIR)/$(CONFIG)


mam:
	@echo "Compiling MaM"
	$(MAKE) -C MaM MAM_USE_SLURM=$(MAM_USE_SLURM) MAM_USE_BARRIERS=$(MAM_USE_BARRIERS) MAM_DEBUG=$(MAM_DEBUG)

sam: mam
	@echo "Compiling SAM"
	$(MAKE) -C SAM

install: mam sam $(CONFIG)
	echo "Done"

#Clean rules
sam_clean:
	@echo "Cleaning SAM"
	$(MAKE) -C SAM clean
mam_clean:
	@echo "Cleaning MaM"
	$(MAKE) -C MaM clean
clean: sam_clean mam_clean
	-rm $(BUILD_DIR)/$(CONFIG)

#Clear rules
sam_clear:
	@echo "Clearing SAM"
	$(MAKE) -C SAM clear
mam_clear:
	@echo "Clearing MaM"
	$(MAKE) -C MaM clear
clear: sam_clear mam_clear
	-rm -rf $(BUILD_DIR)

