ROOT_DIR ?= $(abspath ../../../..)
include $(ROOT_DIR)/mk/toolchain.mk
OBJ_DIR := $(BIN_DIR)/obj/modules/ar928x
MOD_DIR := $(BIN_DIR)/modules
TARGET := $(MOD_DIR)/ar928x.ko
TARGET_A := $(MOD_DIR)/ar928x.a
C_SOURCES := core/ar928x_main.c pci/ar928x_pci.c hw/ar928x_hw.c hw/ar928x_eeprom.c hw/ar928x_mac.c dma/ar928x_dma.c wifi/ar928x_wifi.c
C_OBJECTS := $(C_SOURCES:%.c=$(OBJ_DIR)/%.o)
EXTRA_INCLUDES := -I$(CURDIR)/include
CPPFLAGS += $(EXTRA_INCLUDES)
.PHONY: all clean
all: $(TARGET) $(TARGET_A)
$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(KERNEL_CFLAGS) -c $< -o $@
$(TARGET): $(C_OBJECTS)
	@mkdir -p $(@D)
	$(LD) -r -o $@ $^
$(TARGET_A): $(C_OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^
-include $(C_OBJECTS:.o=.d)
clean:
	rm -rf $(OBJ_DIR) $(TARGET) $(TARGET_A)
