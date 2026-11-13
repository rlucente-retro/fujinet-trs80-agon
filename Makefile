# ==============================================================================
# Makefile - FujiNet TRS-80 / Agon Light 2 Build Pipeline
# Target Platform: Olimex Agon Light 2 (Zilog eZ80F92 in Z80 Mode)
# ==============================================================================

SHELL        := /bin/bash

# Tools & Toolchain Configuration
WINE         ?= wine64
ZDS_DIR      ?= $(HOME)/.wine/drive_c/Zilog/ZDSII_eZ80Acclaim!_5.3.5
AS           := WINEDEBUG=-all $(WINE) "$(ZDS_DIR)/bin/ez80asm.exe"
LD           := WINEDEBUG=-all $(WINE) "$(ZDS_DIR)/bin/eZ80link.exe"
HEX2BIN      ?= hex2bin
PYTHON       ?= python3

# Target Hardware and Memory Configuration
CPU          ?= eZ80F92
LOAD_ADDR    ?= 2600
ENTRY_ADDR   ?= 2600

# Include Paths & Compiler Flags
INCLUDES     := include;.
ASFLAGS      := -cpu:$(CPU) -quiet -include:"$(INCLUDES)"
LDFLAGS      := -format=intel -quiet

# Default Virtual Disk Image for Injection
DEFAULT_DSK  := /Users/richardlucente/development/git/fujinet-firmware/build/dist/SD/boot_volume.dsk
DSK          ?= $(DEFAULT_DSK)

# Discovered Utilities & Include Headers
INC_FILES    := $(wildcard include/*.inc)
UTILS_SRC    := $(wildcard src/*/*.S) $(wildcard src/*/*.s)
UTILS_OBJ    := $(patsubst src/%.S,src/%.obj,$(patsubst src/%.s,src/%.obj,$(UTILS_SRC)))
UTILS_HEX    := $(patsubst %.obj,%.hex,$(UTILS_OBJ))
UTILS_BIN    := $(patsubst %.obj,%.bin,$(UTILS_OBJ))
UTILS_CMD    := $(patsubst %.obj,%.CMD,$(UTILS_OBJ))

.PHONY: all utils test test-packager test-includes inject audit-dsk check-env clean clean-all help

all: check-env utils

# ------------------------------------------------------------------------------
# Command Utilities Build Target
# ------------------------------------------------------------------------------
utils: $(UTILS_CMD)
	@echo "==> All TRSDOS / LS-DOS utilities successfully built."

# Assemble .S -> .obj
src/%.obj: src/%.S $(INC_FILES)
	@echo "==> [AS] $< -> $@"
	@$(AS) $(ASFLAGS) $< -out:$@

# Assemble .s -> .obj (lowercase extension)
src/%.obj: src/%.s $(INC_FILES)
	@echo "==> [AS] $< -> $@"
	@$(AS) $(ASFLAGS) $< -out:$@

# Link .obj -> .hex
src/%.hex: src/%.obj
	@echo "==> [LD] $< -> $@"
	@$(LD) $(LDFLAGS) "src/$*" = "$<"

# Convert Intel HEX -> flat binary .bin at LOAD_ADDR
src/%.bin: src/%.hex
	@echo "==> [HEX2BIN] $< -> $@"
	@$(HEX2BIN) -s $(LOAD_ADDR) -p 00 "$<" > /dev/null

# Package flat binary .bin -> authentic LS-DOS /CMD load module
src/%.CMD: src/%.bin
	@echo "==> [CMD] $< -> $@"
	@$(PYTHON) tools/bin_to_cmd.py --load $(LOAD_ADDR) --entry $(ENTRY_ADDR) "$<" -o "$@"

# ------------------------------------------------------------------------------
# Test Suites Target
# ------------------------------------------------------------------------------
test: test-packager test-includes

test-packager:
	@echo "==> Running CMD packager unit tests..."
	@$(PYTHON) tools/test_bin_to_cmd.py

test-includes:
	@echo "==> Running shared assembly include equates test..."
	@$(PYTHON) tools/test_includes.py

# ------------------------------------------------------------------------------
# Disk Image Injection & Audit Targets
# ------------------------------------------------------------------------------
inject: utils
	@if [ ! -f "$(DSK)" ]; then \
		echo "Error: Target disk image not found at $(DSK)"; \
		echo "Specify a disk image with: make inject DSK=/path/to/image.dsk"; \
		exit 1; \
	fi
	@echo "==> Injecting built utilities into $(DSK)..."
	@for cmd in $(UTILS_CMD); do \
		base=$$(basename "$$cmd" .CMD); \
		echo "    Injecting $$cmd as $$base.CMD..."; \
		$(PYTHON) tools/inject_file_to_dsk.py "$(DSK)" "$$cmd" "$$base" "CMD" || exit 1; \
	done
	@echo "==> Injection complete. Auditing filesystem integrity..."
	@$(PYTHON) tools/audit_filesystem.py "$(DSK)" | grep -E "AUDIT SUMMARY|discovered"

audit-dsk:
	@if [ ! -f "$(DSK)" ]; then \
		echo "Error: Target disk image not found at $(DSK)"; \
		exit 1; \
	fi
	@$(PYTHON) tools/audit_filesystem.py "$(DSK)"

# ------------------------------------------------------------------------------
# Toolchain & Environment Verification
# ------------------------------------------------------------------------------
check-env:
	@if ! command -v $(WINE) > /dev/null 2>&1; then \
		echo "Error: '$(WINE)' not found."; \
		echo "Please install Apple's Game Porting Toolkit wine64 or wine:"; \
		echo "  brew tap gcenx/wine && brew install gcenx/wine/game-porting-toolkit"; \
		exit 1; \
	fi
	@if ! command -v $(HEX2BIN) > /dev/null 2>&1; then \
		echo "Error: '$(HEX2BIN)' not found."; \
		echo "Please install hex2bin via Homebrew:"; \
		echo "  brew install hex2bin"; \
		exit 1; \
	fi
	@if [ ! -f "$(ZDS_DIR)/bin/ez80asm.exe" ]; then \
		echo "Error: ZDS II assembler not found at $(ZDS_DIR)/bin/ez80asm.exe"; \
		echo "Please install ZDS II into Wine prefix (see trs-os-zds/Makefile)"; \
		exit 1; \
	fi
	@if ! command -v $(PYTHON) > /dev/null 2>&1; then \
		echo "Error: '$(PYTHON)' not found."; \
		exit 1; \
	fi

# ------------------------------------------------------------------------------
# Clean Targets
# ------------------------------------------------------------------------------
clean:
	@echo "==> Cleaning intermediate build artifacts..."
	@find src -type f \( -name "*.obj" -o -name "*.lst" -o -name "*.hex" -o -name "*.bin" -o -name "*.map" \) -delete
	@rm -rf tools/__pycache__

clean-all: clean
	@echo "==> Removing packaged .CMD binaries..."
	@find src -type f \( -name "*.CMD" -o -name "*.cmd" \) -delete

help:
	@echo "FujiNet TRS-80 / Agon Light 2 Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all (default)   Check environment and build all utilities"
	@echo "  utils           Build all .CMD utilities in src/"
	@echo "  test            Run packager and include equates unit tests"
	@echo "  inject          Inject built .CMD utilities into DSK image"
	@echo "  audit-dsk       Run filesystem audit on DSK image"
	@echo "  check-env       Verify toolchain prerequisites"
	@echo "  clean           Remove intermediate build artifacts (.obj, .hex, .bin)"
	@echo "  clean-all       Remove all build artifacts including .CMD executables"
	@echo ""
	@echo "Variables:"
	@echo "  DSK             Target .dsk disk image path (default: $(DEFAULT_DSK))"
	@echo "  LOAD_ADDR       Load address in hex (default: 2600)"
	@echo "  ENTRY_ADDR      Execution entry point in hex (default: 2600)"
