ROOT_DIR		:= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR		?= $(ROOT_DIR)build
CROSS				:= $(RISCV)/bin/riscv64-unknown-elf-

TARGET			?= hnsw
RUN_ID			?= $(TARGET)-2

ARCH_FLAGS	= -march=rv64gc -mabi=lp64d -mcmodel=medany
COMMON_FLAGS = -O2 -ffreestanding -fno-pic -fno-pie -fno-builtin \
               -fno-stack-protector -Wall -Wextra
CXX_FLAGS		= -std=c++20 -fno-exceptions -fno-rtti \
               -fno-threadsafe-statics -fno-use-cxa-atexit \
               -fno-unwind-tables -fno-asynchronous-unwind-tables

LIB					= lib/uart.c lib/printk.c lib/heap.c lib/syscalls.c
INIT				= start.S main.c
CXX_SRCS		= coro.cpp
CXX_OBJS		= $(addprefix $(BUILD_DIR)/,$(CXX_SRCS:.cpp=.o))

INCS				= -Iinclude
SRCS				= $(INIT) $(LIB)

.PHONY: all FORCE
all: $(BUILD_DIR)/$(TARGET).bin $(BUILD_DIR)/$(TARGET).dump

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/$(TARGET).elf: $(SRCS) $(CXX_OBJS) linker.ld Makefile FORCE | $(BUILD_DIR)
	$(CROSS)gcc $(ARCH_FLAGS) $(COMMON_FLAGS) \
	  -nostdlib -nostartfiles -static \
		$(INCS) \
	  -DRUN_ID='"$(RUN_ID)"' \
	  -Wl,-T,linker.ld,--build-id=none,--no-relax \
	  -o $@ $(SRCS) $(CXX_OBJS) \
		-Wl,--start-group -lc -lgcc -Wl,--end-group

$(BUILD_DIR)/%.o: %.cpp Makefile FORCE | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CROSS)g++ $(ARCH_FLAGS) $(COMMON_FLAGS) $(CXX_FLAGS) \
	  $(INCS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(CROSS)objcopy -O binary $< $@

$(BUILD_DIR)/$(TARGET).dump: $(BUILD_DIR)/$(TARGET).elf
	$(CROSS)objdump -d $< > $@

-include $(CXX_OBJS:.o=.d)
