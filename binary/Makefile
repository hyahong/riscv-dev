ROOT_DIR		:= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR		?= $(ROOT_DIR)build
CROSS				:= $(RISCV)/bin/riscv64-unknown-elf-

TARGET			?= hnsw
RUN_ID			?= $(TARGET)-1

LIB					= lib/uart.c lib/printk.c
INIT				= start.S main.c

INCS				= -Iinclude
SRCS				= $(INIT) $(LIB)

.PHONY: all FORCE
all: $(BUILD_DIR)/$(TARGET).bin $(BUILD_DIR)/$(TARGET).dump

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/$(TARGET).elf: start.S main.c linker.ld Makefile FORCE | $(BUILD_DIR)
	$(CROSS)gcc -march=rv64ima_zicsr_zifencei -mabi=lp64 -mcmodel=medany \
	  -O2 -fno-pic -fno-pie -fno-builtin -fno-stack-protector \
	  -nostdlib -nostartfiles -static -Wall -Wextra \
		$(INCS) \
	  -DRUN_ID='"$(RUN_ID)"' \
	  -Wl,-T,linker.ld,--build-id=none,--no-relax \
	  -o $@ $(SRCS)

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(CROSS)objcopy -O binary $< $@

$(BUILD_DIR)/$(TARGET).dump: $(BUILD_DIR)/$(TARGET).elf
	$(CROSS)objdump -d $< > $@
