# config.mk - Parse .config file and export configuration variables
#
# This file should be included at the top of all Makefiles to ensure
# consistent use of configuration options from Kconfig/.config
#
# Usage in Makefile:
#   -include $(TOP_DIR)/config.mk

# Find the top directory (where .config is located)
# This is the directory containing config.mk itself (which should be the project root)
# Remove trailing slash for consistency
TOP_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

# Default values if .config doesn't exist or doesn't have these settings
ARCH ?= rv32imc
ABI ?= ilp32

# Parse .config file if it exists
ifneq ($(wildcard $(TOP_DIR)/.config),)
    # Extract CONFIG_RISCV_ARCH value (remove quotes)
    CONFIG_ARCH := $(shell grep '^CONFIG_RISCV_ARCH=' $(TOP_DIR)/.config | cut -d'=' -f2 | tr -d '"')
    ifneq ($(CONFIG_ARCH),)
        ARCH := $(CONFIG_ARCH)
    endif

    # Extract CONFIG_RISCV_ABI value (remove quotes)
    CONFIG_ABI := $(shell grep '^CONFIG_RISCV_ABI=' $(TOP_DIR)/.config | cut -d'=' -f2 | tr -d '"')
    ifneq ($(CONFIG_ABI),)
        ABI := $(CONFIG_ABI)
    endif
endif

# Export for sub-makes
export ARCH
export ABI

# Debug output (comment out in production)
# $(info [config.mk] TOP_DIR=$(TOP_DIR) ARCH=$(ARCH) ABI=$(ABI))
