# Makefile for the MultiFilter LV2 Plugin

# The name of the plugin, used for source, target, and bundle names
BUNDLE_NAME = multifilter

# Standard LV2 installation path (optional, for system-wide install)
PREFIX ?= /usr/lib
LV2_DIR = $(PREFIX)/lv2

# --- Cross-compilation settings for S2400 (aarch64) ---
CC = aarch64-linux-gnu-gcc
# Set the PKG_CONFIG_PATH to find the aarch64 lv2-core libraries
export PKG_CONFIG_PATH = /usr/lib/aarch64-linux-gnu/pkgconfig

# Compiler flags: -g for debugging, -Wall/-Wextra for all warnings,
# -O2 for optimization, -fPIC for position-independent code.
CFLAGS = -g -Wall -Wextra -O2 -fPIC `pkg-config --cflags lv2-core`
LDFLAGS = -shared

# --- File and Directory Definitions ---
BUNDLE_DIR  = $(BUNDLE_NAME).lv2
TARGET_SO   = $(BUNDLE_NAME).so
SRC_C       = $(BUNDLE_NAME).c
TTL_MAIN    = $(BUNDLE_NAME).ttl
TTL_MANIF   = manifest.ttl

# --- Build Targets ---

# Default target: build the complete LV2 bundle
all: $(BUNDLE_DIR)/$(TARGET_SO)

# Rule to build the .so and create the bundle
$(BUNDLE_DIR)/$(TARGET_SO): $(SRC_C) $(TTL_MAIN) $(TTL_MANIF)
	@echo "--- Compiling $(SRC_C) for aarch64 ---"
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET_SO) $(SRC_C)
	
	@echo "--- Creating LV2 Bundle: $(BUNDLE_DIR) ---"
	mkdir -p $(BUNDLE_DIR)
	mv $(TARGET_SO) $(BUNDLE_DIR)/
	cp $(TTL_MANIF) $(BUNDLE_DIR)/
	cp $(TTL_MAIN) $(BUNDLE_DIR)/
	@echo "--- Build complete. ---"

# Clean up all generated files
clean:
	@echo "--- Cleaning up build files ---"
	rm -rf $(BUNDLE_DIR)
	rm -f $(TARGET_SO)

# Install to system directory (optional)
install: all
	mkdir -p $(DESTDIR)$(LV2_DIR)
	cp -r $(BUNDLE_DIR) $(DESTDIR)$(LV2_DIR)

# Uninstall from system directory (optional)
uninstall:
	rm -rf $(DESTDIR)$(LV2_DIR)/$(BUNDLE_DIR)

# Phony targets are not files
.PHONY: all clean install uninstall