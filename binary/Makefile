ROOT_DIR		:= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR		= $(ROOT_DIR)build

TARGET			= hnsw
RUN_ID			= $(TARGET)-2

CROSS				= $(RISCV)/bin/riscv64-unknown-elf-
CC					= $(CROSS)gcc
CXX					= $(CROSS)g++

ARCH_FLAGS	= -march=rv64gc_zicbop -mabi=lp64d -mcmodel=medany
COMMON_FLAGS = -O2 -ffreestanding -fno-pic -fno-pie -fno-builtin \
               -fno-stack-protector -Wall -Wextra
CXX_FLAGS		= -std=c++20 -fno-exceptions -fno-rtti \
               -fno-threadsafe-statics -fno-use-cxa-atexit \
               -fno-unwind-tables -fno-asynchronous-unwind-tables

CPPFLAGS		= -Iinclude -DRUN_ID='"$(RUN_ID)"'
CFLAGS			= $(ARCH_FLAGS) $(COMMON_FLAGS)
CXXFLAGS		= $(ARCH_FLAGS) $(COMMON_FLAGS) $(CXX_FLAGS)
ASFLAGS			= $(ARCH_FLAGS) $(COMMON_FLAGS)
LDFLAGS			= $(ARCH_FLAGS) -nostdlib -nostartfiles -static \
              -Wl,-T,linker.ld,--build-id=none,--no-relax
LDLIBS			= -Wl,--start-group -lc -lgcc -Wl,--end-group

LIB_SRCS		= lib/uart.c lib/printk.c lib/syscalls.c lib/heap.c lib/new.cpp
MAIN_SRCS		= start.S main.c coro.cpp hnsw.cpp

SRCS				= $(LIB_SRCS) $(MAIN_SRCS)
OBJS				= $(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(basename $(SRCS))))
DEPS				= $(OBJS:.o=.d)

.PHONY: all FORCE

all: $(BUILD_DIR)/$(TARGET).bin $(BUILD_DIR)/$(TARGET).dump

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJS) linker.ld Makefile FORCE | $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BUILD_DIR)/%.o: %.c Makefile FORCE | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: %.cpp Makefile FORCE | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: %.S Makefile FORCE | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ASFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(CROSS)objcopy -O binary $< $@

$(BUILD_DIR)/$(TARGET).dump: $(BUILD_DIR)/$(TARGET).elf
	$(CROSS)objdump -d $< > $@

-include $(DEPS)
