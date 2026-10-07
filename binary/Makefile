ROOT_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR ?= $(ROOT_DIR)build
CROSS := $(RISCV)/bin/riscv64-unknown-elf-

TARGET ?= hnsw
RUN_ID ?= $(TARGET)-1

.PHONY: all FORCE
all: $(BUILD_DIR)/$(TARGET).bin $(BUILD_DIR)/$(TARGET).dump

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/$(TARGET).elf: $(ROOT_DIR)start.S $(ROOT_DIR)main.c $(ROOT_DIR)linker.ld $(ROOT_DIR)Makefile FORCE | $(BUILD_DIR)
	$(CROSS)gcc -march=rv64ima_zicsr_zifencei -mabi=lp64 -mcmodel=medany \
	  -O2 -fno-pic -fno-pie -fno-builtin -fno-stack-protector \
	  -nostdlib -nostartfiles -static -Wall -Wextra \
	  -DRUN_ID='"$(RUN_ID)"' \
	  -Wl,-T,$(ROOT_DIR)linker.ld,--build-id=none,--no-relax \
	  -o $@ $(ROOT_DIR)start.S $(ROOT_DIR)main.c

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(CROSS)objcopy -O binary $< $@

$(BUILD_DIR)/$(TARGET).dump: $(BUILD_DIR)/$(TARGET).elf
	$(CROSS)objdump -d $< > $@
