# ==============================================================================
# Makefile - FNBOOT Pre-Boot Visual Manager (Agon Light 2 eZ80 ADL Mode)
# Builds bin/fnboot.bin (/bin/fnboot.bin @ 0x040000) and native host test binary
# ==============================================================================

AGONDEV_TOOLCHAIN ?= $(shell agondev-config --prefix)

TOOLBINDIR   = $(AGONDEV_TOOLCHAIN)/bin
INCLUDEDIR   = $(AGONDEV_TOOLCHAIN)/include
LINKERCONFIG = $(AGONDEV_TOOLCHAIN)/config/linker.conf
LIBDIR       = $(AGONDEV_TOOLCHAIN)/lib

ARCH   = ez80+full
TARGET = ez80-none-elf

CC          = $(TOOLBINDIR)/ez80-none-elf-clang
CFLAGS      = -mllvm -z80-gas-style -mllvm -z80-print-zero-offset \
              -nostdinc -I. -isystem $(INCLUDEDIR) \
              -target $(TARGET) -DAGONDEV -Oz -Wa,-march=$(ARCH) \
              -fno-threadsafe-statics -Wall -Wextra -Wno-unused-parameter
LINKER      = $(TOOLBINDIR)/ez80-none-elf-ld
SETPROGNAME = $(TOOLBINDIR)/agondev-setname

# /bin executables load at 0x040000 (0x040000..0x0AFFFF, 448 KiB ADL RAM)
RAM_START ?= 0x040000
RAM_SIZE  ?= 0x070000
MEMCONFIG = -defsym=RAM_START=$(RAM_START) -defsym=RAM_SIZE=$(RAM_SIZE) -defsym=_has_exit_handler=0
LINKERLIBFLAGS = -L$(LIBDIR) -l agon

SRCS   = main.c uart_fuji.c sdmap.c boot.c
OBJDIR = obj
BINDIR = ../../../bin
OBJS   = $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))

TARGET_BIN    = $(BINDIR)/fnboot.bin
TARGET_NONAME = $(BINDIR)/fnboot.noname.bin
HOST_BIN      = $(OBJDIR)/fnboot_host

.PHONY: all host clean

all: $(TARGET_BIN)

$(TARGET_BIN): $(TARGET_NONAME)
	@cp $< $@
	@$(SETPROGNAME) $@ >/dev/null
	@SZ=$$(wc -c < $@ | tr -d ' '); \
	echo "==> Built $@ ($$SZ bytes, load @ $(RAM_START))"

$(TARGET_NONAME): $(OBJS) | $(BINDIR)
	@echo "==> [LD] $@"
	@$(LINKER) $(MEMCONFIG) -Map=$(BINDIR)/fnboot.map -T $(LINKERCONFIG) --oformat binary -o $@ $^ $(LINKERLIBFLAGS)

$(OBJDIR)/%.o: %.c fnboot.h | $(OBJDIR)
	@echo "==> [CC] $<"
	@$(CC) $(CFLAGS) $< -c -o $@

$(BINDIR):
	@mkdir -p $(BINDIR)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

host: $(HOST_BIN)

$(HOST_BIN): $(SRCS) fnboot.h | $(OBJDIR)
	@echo "==> [HOST CC] $@"
	@cc -DHOST_TEST -O2 -Wall -Wextra -Wno-unused-parameter $(SRCS) -o $@

clean:
	@rm -rf $(OBJDIR) $(TARGET_BIN) $(TARGET_NONAME) $(BINDIR)/fnboot.map
