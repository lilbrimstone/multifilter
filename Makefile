# Makefile for LilBrimstone MultiFilter LV2 Plugin

# --- Configuration ---
# The name of your plugin (used for .so, .lv2 folder, etc.)
PLUGIN_NAME := multifilter

# The directory containing your source files
SRC_DIR := src

# The name of the LV2 bundle that will be created
BUNDLE_NAME := $(PLUGIN_NAME).lv2

# --- Compiler and Flags ---
CC := aarch64-linux-gnu-gcc
CFLAGS := -O3 -fPIC -shared -Wall -Wextra -I.
PKG_CONFIG := aarch64-linux-gnu-pkg-config
LV2_CFLAGS := $(shell $(PKG_CONFIG) --cflags lv2-plugin)
LV2_LIBS := $(shell $(PKG_CONFIG) --libs lv2-plugin)

# --- Files ---
# Source files are now read from the SRC_DIR
C_SRC := $(SRC_DIR)/$(PLUGIN_NAME).c
TTL_SRC := $(SRC_DIR)/$(PLUGIN_NAME).ttl
MANIFEST_SRC := $(SRC_DIR)/manifest.ttl

# Destination files are in the BUNDLE_NAME directory
SO_DEST := $(BUNDLE_NAME)/$(PLUGIN_NAME).so
TTL_DEST := $(BUNDLE_NAME)/$(PLUGIN_NAME).ttl
MANIFEST_DEST := $(BUNDLE_NAME)/manifest.ttl

# --- Rules ---
.PHONY: all clean install

# Default rule: build everything
all: $(SO_DEST) $(TTL_DEST) $(MANIFEST_DEST)

# Rule to build the .so file
$(SO_DEST): $(C_SRC) | $(BUNDLE_NAME)
	$(CC) $(CFLAGS) $(LV2_CFLAGS) -o $@ $< $(LV2_LIBS)
	@echo "Compiled:" $@

# Rule to copy the main .ttl file
$(TTL_DEST): $(TTL_SRC) | $(BUNDLE_NAME)
	@cp $< $@
	@echo "Copied TTL:" $@

# Rule to copy the manifest.ttl file
$(MANIFEST_DEST): $(MANIFEST_SRC) | $(BUNDLE_NAME)
	@cp $< $@
	@echo "Copied Manifest:" $@

# Rule to create the bundle directory if it doesn't exist
$(BUNDLE_NAME):
	@mkdir -p $@

# Rule to clean up build artifacts
clean:
	@rm -rf $(BUNDLE_NAME)
	@echo "Cleaned build artifacts."

# Rule to install to S2400 (optional)
install: all
	@cp -r $(BUNDLE_NAME) /mnt/s2400/lv2/
	@echo "Installed $(BUNDLE_NAME) to S2400."