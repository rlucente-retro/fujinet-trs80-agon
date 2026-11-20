# ==============================================================================
# Makefile - Unified *trs80 Quark MOS Utility (Agon Light 2 eZ80 ADL Mode)
# Builds bin/trs80.bin (MOSlet @ 0x0B0000, 32 KiB max) and host test binary
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

# Executables in /bin load at 0x040000 (0x040000..0x0AFFFF, 448 KiB ADL RAM)
RAM_START ?= 0x040000
RAM_SIZE  ?= 0x070000
MEMCONFIG = -defsym=RAM_START=$(RAM_START) -defsym=RAM_SIZE=$(RAM_SIZE) -defsym=_has_exit_handler=0
LINKERLIBFLAGS = -L$(LIBDIR) -l agon

SRCS   = main.c platform_io.c disk.c lsdos.c map.c
OBJDIR = obj
BINDIR = ../../../bin
OBJS   = $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))

TARGET_BIN    = $(BINDIR)/trs80.bin
TARGET_NONAME = $(BINDIR)/trs80.noname.bin
HOST_BIN      = $(OBJDIR)/trs80_host

.PHONY: all host test clean

all: $(TARGET_BIN)

$(TARGET_BIN): $(TARGET_NONAME)
	@cp $< $@
	@$(SETPROGNAME) $@ >/dev/null
	@SZ=$$(wc -c < $@ | tr -d ' '); \
	echo "==> Built $@ ($$SZ bytes, load @ $(RAM_START))"

$(TARGET_NONAME): $(OBJS) | $(BINDIR)
	@echo "==> [LD] $@"
	@$(LINKER) $(MEMCONFIG) -Map=$(BINDIR)/trs80.map -T $(LINKERCONFIG) --oformat binary -o $@ $^ $(LINKERLIBFLAGS)

$(OBJDIR)/%.o: %.c trs80.h | $(OBJDIR)
	@echo "==> [CC] $<"
	@$(CC) $(CFLAGS) $< -c -o $@

$(BINDIR):
	@mkdir -p $(BINDIR)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

# Native host build for automated parity & regression testing
host: $(HOST_BIN)

$(HOST_BIN): $(SRCS) trs80.h | $(OBJDIR)
	@echo "==> [HOST CC] $@"
	@cc -DHOST_TEST -O2 -Wall -Wextra -Wno-unused-parameter $(SRCS) -o $@

clean:
	@rm -rf $(OBJDIR) $(TARGET_BIN) $(TARGET_NONAME) $(BINDIR)/trs80.map
