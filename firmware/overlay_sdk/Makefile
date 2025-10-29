#===============================================================================
# Olimex iCE40HX8K-EVB RISC-V Platform - Overlay SDK
# Makefile - Top-level SDK control
#
# Copyright (c) October 2025 Michael Wolak
# Email: mikewolak@gmail.com, mike@epromfoundry.com
#===============================================================================

# Include all SDK functionality
include Makefile.overlay

# Default target
.DEFAULT_GOAL := help

# Build all projects
PROJECTS := heap_test hello_world hexedit mandelbrot_fixed mandelbrot_float printf_demo timer_test

.PHONY: all $(PROJECTS)

all: $(PROJECTS)

$(PROJECTS):
	@echo "Building overlay project: $@"
	@$(MAKE) -C projects/$@ all
	@echo ""
