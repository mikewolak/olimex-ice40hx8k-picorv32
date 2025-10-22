# lwIP Demo Applications Makefile
# Include parent firmware Makefile for build system

PARENT_DIR = ../..
include $(PARENT_DIR)/Makefile

.PHONY: slip_echo_server slip_perf_server all

all: slip_echo_server slip_perf_server

slip_echo_server:
	@echo "Building SLIP echo server..."
	$(MAKE) -C $(PARENT_DIR) TARGET=lwIP/demos/slip_echo_server USE_LWIP=1 USE_NEWLIB=1 single-target

slip_perf_server:
	@echo "Building SLIP performance server..."
	$(MAKE) -C $(PARENT_DIR) TARGET=lwIP/demos/slip_perf_server USE_LWIP=1 USE_NEWLIB=1 single-target
