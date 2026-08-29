ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/mk/toolchain.mk

OBJ_DIR := $(BIN_DIR)/obj/programs
PROGRAM_DIR := $(BIN_DIR)/programs
LIB_DIR := $(BIN_DIR)/lib
LIBPUREC := $(LIB_DIR)/libpurec.a
LIBPUREGUI := $(LIB_DIR)/libpuregui.a
LIBPGUIW := $(LIB_DIR)/libpguiw.a
LINKER_SCRIPT := $(ROOT_DIR)/linker-userspace.ld

PLAIN_SOURCES := \
	init/main.c \
	game/snake/main.c \
	nano/main.c \
	nano/editor.c \
	system/main.c \
	system/commands.c \
	system/filesystem.c \
	system/system.c
GENERAL_SOURCES := \
	installer/main.c \
	installer/progress.c \
	terminal/main.c \
	terminal/shell.c \
	terminal/path.c \
	terminal/environment.c \
	terminal/window.c \
	nano/window.c \
	gui_demo/main.c \
	files/main.c \
	files/app.c \
	files/model.c \
	files/path.c \
	files/view.c \
	settings/main.c \
	settings/core/model.c \
	settings/core/app.c \
	settings/pages/audio.c \
	settings/pages/wifi.c \
	monitor/main.c
PLAIN_OBJECTS := $(PLAIN_SOURCES:%.c=$(OBJ_DIR)/%.o)
GENERAL_OBJECTS := $(GENERAL_SOURCES:%.c=$(OBJ_DIR)/%.o)
TARGETS := \
	$(PROGRAM_DIR)/init \
	$(PROGRAM_DIR)/installer \
	$(PROGRAM_DIR)/snake \
	$(PROGRAM_DIR)/terminal \
	$(PROGRAM_DIR)/nano \
	$(PROGRAM_DIR)/system \
	$(PROGRAM_DIR)/gui-demo \
	$(PROGRAM_DIR)/files \
	$(PROGRAM_DIR)/settings \
	$(PROGRAM_DIR)/monitor

.NOTPARALLEL:
.PHONY: all
all: $(TARGETS)

-include $(PLAIN_OBJECTS:.o=.d) $(GENERAL_OBJECTS:.o=.d)

$(GENERAL_OBJECTS): PRIVATE_CFLAGS := -mgeneral-regs-only

$(LIBPUREC):
	$(MAKE) -C $(ROOT_DIR)/src/libc

$(LIBPUREGUI) $(LIBPGUIW):
	$(MAKE) -C $(ROOT_DIR)/src/libgui

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/programs/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(ROOT_DIR)/src/programs/settings/include $(DEPFLAGS) $(USER_CFLAGS) $(PRIVATE_CFLAGS) -c $< -o $@

$(PROGRAM_DIR)/init: $(OBJ_DIR)/init/main.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/installer: $(OBJ_DIR)/installer/main.o $(OBJ_DIR)/installer/progress.o $(OBJ_DIR)/terminal/window.o $(LIBPUREGUI) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/snake: $(OBJ_DIR)/game/snake/main.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/terminal: $(OBJ_DIR)/terminal/main.o $(OBJ_DIR)/terminal/shell.o $(OBJ_DIR)/terminal/path.o $(OBJ_DIR)/terminal/environment.o $(OBJ_DIR)/terminal/window.o $(LIBPUREGUI) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/nano: $(OBJ_DIR)/nano/main.o $(OBJ_DIR)/nano/editor.o $(OBJ_DIR)/nano/window.o $(LIBPUREGUI) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/system: $(OBJ_DIR)/system/main.o $(OBJ_DIR)/system/commands.o $(OBJ_DIR)/system/filesystem.o $(OBJ_DIR)/system/system.o $(OBJ_DIR)/terminal/path.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/gui-demo: $(OBJ_DIR)/gui_demo/main.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/files: $(OBJ_DIR)/files/main.o $(OBJ_DIR)/files/app.o $(OBJ_DIR)/files/model.o $(OBJ_DIR)/files/path.o $(OBJ_DIR)/files/view.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/settings: $(OBJ_DIR)/settings/main.o $(OBJ_DIR)/settings/core/model.o $(OBJ_DIR)/settings/core/app.o $(OBJ_DIR)/settings/pages/audio.o $(OBJ_DIR)/settings/pages/wifi.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/monitor: $(OBJ_DIR)/monitor/main.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)
