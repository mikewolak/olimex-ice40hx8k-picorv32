# Olimex iCE40HX8K PicoRV32 Build System
# Main Makefile - User interface

.PHONY: all default firmware help clean distclean mrproper menuconfig defconfig config-if-needed generate
.PHONY: bootloader upload-tool test-generators lwip-tools slip-perf-client slip-perf-server
.PHONY: toolchain-riscv toolchain-fpga toolchain-download toolchain-check toolchain-if-needed verify-platform
.PHONY: fetch-picorv32 build-newlib check-newlib newlib-if-needed
.PHONY: freertos-download freertos-clean freertos-check freertos-if-needed
.PHONY: lwip-download lwip-clean lwip-check lwip-if-needed
.PHONY: fw-led-blink fw-timer-clock fw-coop-tasks fw-hexedit fw-heap-test fw-algo-test
.PHONY: fw-mandelbrot-fixed fw-mandelbrot-float firmware-all firmware-bare firmware-newlib newlib-if-needed
.PHONY: firmware-freertos firmware-freertos-if-needed
.PHONY: bitstream synth pnr pnr-sa pack timing artifacts

# Detect number of cores
NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
export NPROC

# Toolchain detection and PATH setup
ifneq (,$(wildcard build/toolchain/bin/riscv64-unknown-elf-gcc))
    PREFIX := build/toolchain/bin/riscv64-unknown-elf-
else ifneq (,$(wildcard build/toolchain/bin/riscv32-unknown-elf-gcc))
    PREFIX := build/toolchain/bin/riscv32-unknown-elf-
else
    PREFIX := riscv64-unknown-elf-
endif

# Toolchain paths - prepend project tools to PATH
# This ensures we use downloaded tools instead of system tools
export PATH := $(CURDIR)/downloads/oss-cad-suite/bin:$(CURDIR)/build/toolchain/bin:$(CURDIR)/build/toolchain:$(PATH)

# Ensure .config exists before building
config-if-needed:
	@if [ ! -f .config ]; then \
		echo "=========================================" ; \
		echo ".config not found - creating default configuration..." ; \
		echo "=========================================" ; \
		$(MAKE) defconfig; \
	fi

# Default target: build everything
default: all

all: config-if-needed toolchain-if-needed bootloader firmware-bare newlib-if-needed firmware-newlib freertos-if-needed firmware-freertos-if-needed lwip-if-needed firmware bitstream upload-tool lwip-tools artifacts
	@echo ""
	@echo "========================================="
	@echo "✓ Build Complete!"
	@echo "========================================="
	@echo ""
	@echo "Build artifacts collected in artifacts/ directory"
	@echo "See artifacts/build-report.txt for detailed build information"
	@echo ""
	@echo "Next steps:"
	@echo "  1. Program FPGA:    iceprog artifacts/gateware/ice40_picorv32.bin"
	@echo "  2. Upload firmware: artifacts/host/fw_upload -p /dev/ttyUSB0 artifacts/firmware/<name>.bin"
	@echo ""

firmware: generate newlib-if-needed freertos-if-needed lwip-if-needed
	@echo ""
	@echo "========================================="
	@echo "Building ALL Firmware Targets"
	@echo "========================================="
	@echo ""
	@$(MAKE) -C firmware firmware
	@echo ""
	@echo "========================================="
	@echo "✓ Firmware Build Complete!"
	@echo "========================================="
	@echo ""
	@echo "All firmware built in firmware/ directory"
	@echo "Run 'make artifacts' to collect binaries"
	@echo ""

help:
	@echo "========================================="
	@echo "Olimex iCE40HX8K PicoRV32 Build System"
	@echo "========================================="
	@echo ""
	@echo "Configuration:"
	@echo "  make menuconfig      - Configure system (requires kconfig-mconf)"
	@echo "  make defconfig       - Load default config"
	@echo "  make savedefconfig   - Save current config as defconfig"
	@echo ""
	@echo "Toolchain Management:"
	@echo "  make toolchain-check    - Check for required tools"
	@echo "  make toolchain-download - Download pre-built toolchains (~5-10 min)"
	@echo "  make toolchain-riscv    - Build RISC-V GCC from source (~1-2 hours)"
	@echo "  make toolchain-fpga     - Build Yosys/NextPNR/IceStorm (~30-45 min)"
	@echo "  make fetch-picorv32     - Download PicoRV32 core"
	@echo "  make build-newlib       - Build newlib C library (~30-45 min)"
	@echo "  make newlib-if-needed       - Check if newlib is installed"
	@echo "  make freertos-download  - Download FreeRTOS kernel (~1 min)"
	@echo "  make freertos-if-needed     - Check if FreeRTOS is installed"
	@echo "  make freertos-clean     - Remove FreeRTOS kernel"
	@echo "  make lwip-download      - Download lwIP TCP/IP stack (~1 min)"
	@echo "  make lwip-check         - Check if lwIP is installed"
	@echo "  make lwip-clean         - Remove lwIP TCP/IP stack"
	@echo ""
	@echo "Code Generation:"
	@echo "  make generate        - Generate platform files from .config"
	@echo "  make test-generators - Test generator scripts"
	@echo ""
	@echo "Building:"
	@echo "  make                      - Build everything (firmware + bitstream + tools)"
	@echo "  make firmware             - Build firmware only (fast, no synthesis)"
	@echo "  make bootloader           - Build bootloader"
	@echo "  make firmware-all         - Build all firmware targets"
	@echo "  make bitstream            - Build FPGA bitstream (synth + pnr + pack)"
	@echo "  make synth                - Synthesis only (Verilog -> JSON)"
	@echo "  make pnr                  - Place and route (JSON -> ASC)"
	@echo "  make pnr-sa               - Place and route with SA placer"
	@echo "  make pack                 - Pack bitstream (ASC -> BIN)"
	@echo "  make timing               - Timing analysis"
	@echo "  make upload-tool          - Build firmware uploader"
	@echo "  make lwip-tools           - Build lwIP performance test tools"
	@echo "  make slip-perf-client     - Build SLIP perf client only"
	@echo "  make slip-perf-server     - Build SLIP perf server only"
	@echo ""
	@echo "Firmware Targets (bare metal):"
	@echo "  make fw-led-blink         - LED blink demo"
	@echo "  make fw-timer-clock       - Timer clock demo"
	@echo ""
	@echo "Firmware Targets (newlib):"
	@echo "  make fw-hexedit           - Hex editor with file upload"
	@echo "  make fw-heap-test         - Heap allocation test"
	@echo "  make fw-algo-test         - Algorithm test suite"
	@echo "  make fw-mandelbrot-fixed  - Mandelbrot (fixed point)"
	@echo "  make fw-mandelbrot-float  - Mandelbrot (floating point)"
	@echo ""
	@echo "Clean:"
	@echo "  make clean           - Remove build artifacts"
	@echo "  make distclean       - Remove config + artifacts"
	@echo "  make mrproper        - Complete clean (pristine)"
	@echo ""
	@echo "Quick Start (Fresh Machine):"
	@echo "  1. make defconfig"
	@echo "  2. make toolchain-download  (or toolchain-riscv + toolchain-fpga)"
	@echo "  3. make generate"
	@echo "  4. make  (when build system is complete)"
	@echo ""

# Configuration targets
menuconfig:
	@if ! command -v kconfig-mconf >/dev/null 2>&1; then \
		echo "ERROR: kconfig-mconf not found"; \
		echo "Install with: sudo apt install kconfig-frontends"; \
		exit 1; \
	fi
	@kconfig-mconf Kconfig

defconfig:
	@echo "Loading default configuration..."
	@cp configs/defconfig .config
	@echo "✓ Loaded configs/defconfig"
	@echo ""
	@echo "Next steps:"
	@echo "  1. make toolchain-check     # Check if tools are installed"
	@echo "  2. make toolchain-download  # Or build from source"
	@echo "  3. make generate            # Create platform files"

savedefconfig:
	@if [ ! -f .config ]; then \
		echo "ERROR: No .config found. Run 'make menuconfig' or 'make defconfig' first."; \
		exit 1; \
	fi
	@echo "Saving current config as defconfig..."
	@cp .config configs/defconfig
	@echo "✓ Saved to configs/defconfig"

# ============================================================================
# Toolchain Management
# ============================================================================

verify-platform:
	@if [ -f scripts/verify-platform.sh ]; then \
		chmod +x scripts/verify-platform.sh; \
		bash scripts/verify-platform.sh; \
	else \
		echo "⚠ Warning: scripts/verify-platform.sh not found, skipping platform verification"; \
	fi

toolchain-check: verify-platform
	@echo "========================================="
	@echo "Checking for required tools"
	@echo "========================================="
	@echo ""
	@echo "RISC-V Toolchain:"
	@if command -v $(PREFIX)gcc >/dev/null 2>&1; then \
		$(PREFIX)gcc --version | head -1; \
		echo "✓ Found: $(PREFIX)gcc"; \
	else \
		echo "✗ Not found: $(PREFIX)gcc"; \
		echo "  Run: make toolchain-download (fast)"; \
		echo "   or: make toolchain-riscv (build from source)"; \
	fi
	@echo ""
	@echo "FPGA Tools:"
	@if command -v yosys >/dev/null 2>&1; then \
		yosys -V | head -1; \
		echo "✓ Found: yosys"; \
	else \
		echo "✗ Not found: yosys"; \
		echo "  Run: make toolchain-download (fast)"; \
		echo "   or: make toolchain-fpga (build from source)"; \
	fi
	@if command -v nextpnr-ice40 >/dev/null 2>&1; then \
		nextpnr-ice40 --version 2>&1 | head -1; \
		echo "✓ Found: nextpnr-ice40"; \
	else \
		echo "✗ Not found: nextpnr-ice40"; \
	fi
	@if command -v icepack >/dev/null 2>&1; then \
		echo "✓ Found: icepack"; \
	else \
		echo "✗ Not found: icepack"; \
	fi

# Auto-download toolchains if needed (both RISC-V and FPGA tools)
toolchain-if-needed:
	@NEED_DOWNLOAD=0; \
	RISCV_OK=0; \
	FPGA_OK=0; \
	\
	echo "Checking for installed toolchains..."; \
	\
	if [ -f build/toolchain/bin/riscv32-unknown-elf-gcc ] && \
	   [ -f build/toolchain/bin/riscv32-unknown-elf-as ] && \
	   [ -f build/toolchain/bin/riscv32-unknown-elf-ld ] && \
	   [ -f build/toolchain/bin/riscv32-unknown-elf-objcopy ]; then \
		echo "  ✓ RISC-V toolchain (rv32) found"; \
		RISCV_OK=1; \
	elif [ -f build/toolchain/bin/riscv64-unknown-elf-gcc ] && \
	     [ -f build/toolchain/bin/riscv64-unknown-elf-as ] && \
	     [ -f build/toolchain/bin/riscv64-unknown-elf-ld ] && \
	     [ -f build/toolchain/bin/riscv64-unknown-elf-objcopy ]; then \
		echo "  ✓ RISC-V toolchain (rv64) found"; \
		RISCV_OK=1; \
	else \
		echo "  ✗ RISC-V toolchain not found or incomplete"; \
		NEED_DOWNLOAD=1; \
	fi; \
	\
	if [ -f downloads/oss-cad-suite/bin/yosys ] && \
	   [ -f downloads/oss-cad-suite/bin/nextpnr-ice40 ] && \
	   [ -f downloads/oss-cad-suite/bin/icepack ] && \
	   [ -f downloads/oss-cad-suite/bin/icetime ]; then \
		echo "  ✓ FPGA tools (OSS CAD Suite) found"; \
		FPGA_OK=1; \
	else \
		echo "  ✗ FPGA tools (OSS CAD Suite) not found or incomplete"; \
		NEED_DOWNLOAD=1; \
	fi; \
	\
	if [ $$NEED_DOWNLOAD -eq 1 ]; then \
		echo ""; \
		echo "Downloading required toolchains..."; \
		$(MAKE) toolchain-download; \
	else \
		echo ""; \
		echo "All toolchains already installed"; \
	fi

toolchain-download:
	@echo "Downloading pre-built toolchains..."
	@./scripts/download_prebuilt_tools.sh

toolchain-riscv: .config
	@echo "Building RISC-V toolchain from source..."
	@./scripts/build_riscv_toolchain.sh

toolchain-fpga:
	@echo "Building FPGA tools from source..."
	@./scripts/build_fpga_tools.sh

fetch-picorv32: .config
	@./scripts/fetch_picorv32.sh

build-newlib: .config
	@if [ ! -f .config ]; then \
		echo "ERROR: No .config found. Run 'make defconfig' first."; \
		exit 1; \
	fi
	@. ./.config && \
	if [ "$$CONFIG_BUILD_NEWLIB" != "y" ]; then \
		echo "ERROR: Newlib build not enabled in configuration"; \
		echo "Run 'make menuconfig' and enable 'Build newlib C library'"; \
		exit 1; \
	fi
	@echo "Building newlib C library..."
	@./scripts/build_newlib.sh

check-newlib:
	@if [ -d build/sysroot ]; then \
		echo "✓ Newlib installed at build/sysroot"; \
		echo ""; \
		echo "Libraries:"; \
		find build/sysroot -name "*.a" | head -5; \
	else \
		echo "✗ Newlib not found"; \
		echo "  (Will auto-download if needed by firmware build)"; \
	fi

# ============================================================================
# FreeRTOS RTOS Kernel
# ============================================================================

FREERTOS_DIR = downloads/freertos
FREERTOS_VERSION ?= main

freertos-download:
	@echo "========================================="
	@echo "Downloading FreeRTOS Kernel"
	@echo "========================================="
	@if [ -d "$(FREERTOS_DIR)" ]; then \
		echo "FreeRTOS already downloaded"; \
		if [ -f "$(FREERTOS_DIR)/.version" ]; then \
			echo "Current version: $$(cat $(FREERTOS_DIR)/.version)"; \
		fi; \
	else \
		echo "Cloning FreeRTOS-Kernel from GitHub..."; \
		echo "Version: $(FREERTOS_VERSION)"; \
		mkdir -p downloads; \
		git clone --depth 1 --branch $(FREERTOS_VERSION) \
			https://github.com/FreeRTOS/FreeRTOS-Kernel.git $(FREERTOS_DIR); \
		echo "$(FREERTOS_VERSION)" > $(FREERTOS_DIR)/.version; \
		echo "✓ FreeRTOS Kernel downloaded to $(FREERTOS_DIR)"; \
	fi

freertos-check:
	@if [ -d "$(FREERTOS_DIR)" ]; then \
		echo "✓ FreeRTOS Kernel found at $(FREERTOS_DIR)"; \
		if [ -f "$(FREERTOS_DIR)/.version" ]; then \
			echo "  Version: $$(cat $(FREERTOS_DIR)/.version)"; \
		fi; \
		echo ""; \
		echo "Kernel files:"; \
		ls -lh $(FREERTOS_DIR)/*.c 2>/dev/null | head -5; \
	else \
		echo "✗ FreeRTOS Kernel not found"; \
		echo "  (Will auto-download if needed by firmware build)"; \
	fi

freertos-clean:
	@echo "Removing FreeRTOS Kernel..."
	@rm -rf $(FREERTOS_DIR)
	@echo "✓ FreeRTOS Kernel removed"

# ============================================================================
# lwIP TCP/IP Stack
# ============================================================================

LWIP_DIR = downloads/lwip
LWIP_VERSION ?= STABLE-2_2_0_RELEASE

lwip-download:
	@echo "========================================="
	@echo "Downloading lwIP TCP/IP Stack"
	@echo "========================================="
	@if [ -d "$(LWIP_DIR)" ]; then \
		echo "lwIP already downloaded"; \
		if [ -f "$(LWIP_DIR)/.version" ]; then \
			echo "Current version: $$(cat $(LWIP_DIR)/.version)"; \
		fi; \
	else \
		echo "Cloning lwIP from GitHub..."; \
		echo "Version: $(LWIP_VERSION)"; \
		mkdir -p downloads; \
		git clone --depth 1 --branch $(LWIP_VERSION) \
			https://github.com/lwip-tcpip/lwip.git $(LWIP_DIR); \
		echo "$(LWIP_VERSION)" > $(LWIP_DIR)/.version; \
		echo "✓ lwIP TCP/IP Stack downloaded to $(LWIP_DIR)"; \
	fi

lwip-check:
	@if [ -d "$(LWIP_DIR)" ]; then \
		echo "✓ lwIP TCP/IP Stack found at $(LWIP_DIR)"; \
		if [ -f "$(LWIP_DIR)/.version" ]; then \
			echo "  Version: $$(cat $(LWIP_DIR)/.version)"; \
		fi; \
		echo ""; \
		echo "Core files:"; \
		ls -lh $(LWIP_DIR)/src/core/*.c 2>/dev/null | head -5; \
	else \
		echo "✗ lwIP TCP/IP Stack not found"; \
		echo "Run: make lwip-download"; \
	fi

lwip-clean:
	@echo "Removing lwIP TCP/IP Stack..."
	@rm -rf $(LWIP_DIR)
	@echo "✓ lwIP TCP/IP Stack removed"

# Check and download lwIP if needed
lwip-if-needed: toolchain-if-needed
	@if [ ! -d downloads/lwip/src ]; then \
		echo "lwIP not found, downloading..."; \
		$(MAKE) lwip-download; \
	fi

# ============================================================================
# Code Generation
# ============================================================================

generate: .config
	@./scripts/generate_all.sh

test-generators: defconfig
	@echo "========================================="
	@echo "Testing generator scripts"
	@echo "========================================="
	@./scripts/generate_all.sh
	@echo ""
	@echo "Generated files:"
	@ls -lh build/generated/
	@echo ""
	@echo "Preview of generated files:"
	@echo ""
	@echo "--- start.S (first 20 lines) ---"
	@head -20 build/generated/start.S
	@echo ""
	@echo "--- linker.ld (memory sections) ---"
	@grep -A 5 "MEMORY" build/generated/linker.ld
	@echo ""
	@echo "--- platform.h (defines) ---"
	@grep "#define" build/generated/platform.h | head -15
	@echo ""
	@echo "✓ Generator scripts working correctly"

# ============================================================================
# Build Targets
# ============================================================================

# Bootloader (required before bitstream - embedded in BRAM)
bootloader: generate
	@echo "========================================="
	@echo "Building Bootloader"
	@echo "========================================="
	@$(MAKE) -C bootloader
	@echo ""
	@echo "✓ Bootloader built: bootloader/bootloader.hex"
	@echo "  (Embedded in BRAM during bitstream synthesis)"

# Bare metal firmware targets (no newlib, no syscalls)
firmware-bare: fw-led-blink fw-timer-clock fw-coop-tasks fw-button-demo fw-irq-counter-test fw-irq-timer-test fw-softirq-test

fw-led-blink: generate
	@$(MAKE) -C firmware TARGET=led_blink USE_NEWLIB=0 single-target

fw-timer-clock: generate
	@$(MAKE) -C firmware TARGET=timer_clock USE_NEWLIB=0 single-target

fw-coop-tasks: generate
	@$(MAKE) -C firmware TARGET=coop_tasks USE_NEWLIB=0 single-target

fw-button-demo: generate
	@$(MAKE) -C firmware TARGET=button_demo USE_NEWLIB=0 single-target

fw-irq-counter-test: generate
	@$(MAKE) -C firmware TARGET=irq_counter_test USE_NEWLIB=0 single-target

fw-irq-timer-test: generate
	@$(MAKE) -C firmware TARGET=irq_timer_test USE_NEWLIB=0 single-target

fw-softirq-test: generate
	@$(MAKE) -C firmware TARGET=softirq_test USE_NEWLIB=0 single-target

# Newlib firmware targets (require newlib)
fw-hexedit: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=hexedit USE_NEWLIB=1 single-target

fw-heap-test: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=heap_test USE_NEWLIB=1 single-target

fw-algo-test: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=algo_test USE_NEWLIB=1 single-target

fw-mandelbrot-fixed: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=mandelbrot_fixed USE_NEWLIB=1 single-target

fw-mandelbrot-float: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=mandelbrot_float USE_NEWLIB=1 single-target

fw-hexedit-fast: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=hexedit_fast USE_NEWLIB=1 single-target

fw-math-test: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=math_test USE_NEWLIB=1 single-target

fw-memory-test-baseline: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=memory_test_baseline USE_NEWLIB=1 single-target

fw-memory-test-baseline-safe: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=memory_test_baseline_safe USE_NEWLIB=1 single-target

fw-memory-test-debug: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=memory_test_debug USE_NEWLIB=1 single-target

fw-memory-test-minimal: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=memory_test_minimal USE_NEWLIB=1 single-target

fw-memory-test-simple: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=memory_test_simple USE_NEWLIB=1 single-target

fw-printf-test: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=printf_test USE_NEWLIB=1 single-target

fw-spi-test: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=spi_test USE_NEWLIB=1 single-target

fw-stdio-test: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=stdio_test USE_NEWLIB=1 single-target

fw-uart-echo-test: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=uart_echo_test USE_NEWLIB=1 single-target

fw-verify-algo: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=verify_algo USE_NEWLIB=1 single-target

fw-verify-math: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=verify_math USE_NEWLIB=1 single-target

fw-interactive: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=interactive USE_NEWLIB=1 single-target

fw-interactive-test: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=interactive_test USE_NEWLIB=1 single-target

fw-syscall-test: generate newlib-if-needed
	@$(MAKE) -C firmware TARGET=syscall_test USE_NEWLIB=1 single-target

# FreeRTOS firmware targets (require newlib and FreeRTOS)
fw-freertos-minimal: generate newlib-if-needed freertos-if-needed
	@$(MAKE) -C firmware TARGET=freertos_minimal USE_FREERTOS=1 USE_NEWLIB=1 single-target

fw-freertos-demo: generate newlib-if-needed freertos-if-needed
	@$(MAKE) -C firmware TARGET=freertos_demo USE_FREERTOS=1 USE_NEWLIB=1 single-target

fw-freertos-printf-demo: generate newlib-if-needed freertos-if-needed
	@$(MAKE) -C firmware TARGET=freertos_printf_demo USE_FREERTOS=1 USE_NEWLIB=1 single-target

fw-freertos-curses-demo: generate newlib-if-needed freertos-if-needed
	@$(MAKE) -C firmware TARGET=freertos_curses_demo USE_FREERTOS=1 USE_NEWLIB=1 single-target

fw-freertos-tasks-demo: generate newlib-if-needed freertos-if-needed
	@$(MAKE) -C firmware TARGET=freertos_tasks_demo USE_FREERTOS=1 USE_NEWLIB=1 single-target

fw-freertos-queue-demo: generate newlib-if-needed freertos-if-needed
	@$(MAKE) -C firmware TARGET=freertos_queue_demo USE_FREERTOS=1 USE_NEWLIB=1 single-target

# Build all FreeRTOS firmware
firmware-freertos: fw-freertos-minimal fw-freertos-demo fw-freertos-printf-demo fw-freertos-tasks-demo fw-freertos-queue-demo fw-freertos-curses-demo

# Build newlib firmware (conditional on newlib being installed)
firmware-newlib: fw-hexedit fw-heap-test fw-algo-test fw-mandelbrot-fixed fw-mandelbrot-float fw-hexedit-fast fw-math-test fw-memory-test-baseline fw-memory-test-baseline-safe fw-memory-test-debug fw-memory-test-minimal fw-memory-test-simple fw-printf-test fw-spi-test fw-stdio-test fw-uart-echo-test fw-verify-algo fw-verify-math fw-interactive fw-interactive-test fw-syscall-test

# Build all overlay projects
firmware-overlays: newlib-if-needed
	@echo "========================================="
	@echo "Building all overlay projects"
	@echo "========================================="
	@for project in hello_world heap_test hexedit mandelbrot_fixed mandelbrot_float printf_demo timer_test; do \
		echo "Building overlay: $$project"; \
		$(MAKE) -C firmware/overlay_sdk/projects/$$project all || exit 1; \
		echo "Validating PIC for: $$project"; \
		cd firmware/overlay_sdk && ./validate_pic.sh projects/$$project/$$project.elf || exit 1; \
		cd ../..; \
	done
	@echo "✓ All overlays built and validated successfully"

# Check and build newlib if needed
newlib-if-needed: toolchain-if-needed
	@. ./.config && \
	if [ "$$CONFIG_BUILD_NEWLIB" = "y" ]; then \
		if [ ! -d build/sysroot/riscv64-unknown-elf/include ]; then \
			echo "Newlib not found, building..."; \
			$(MAKE) build-newlib; \
		fi; \
	fi

# Check and download FreeRTOS if needed
freertos-if-needed: toolchain-if-needed
	@. ./.config && \
	if [ "$$CONFIG_FREERTOS" = "y" ]; then \
		if [ ! -d downloads/freertos/include ]; then \
			echo "FreeRTOS not found, downloading..."; \
			$(MAKE) freertos-download; \
		fi; \
	fi

# Build FreeRTOS firmware if enabled
firmware-freertos-if-needed:
	@. ./.config && \
	if [ "$$CONFIG_FREERTOS" = "y" ]; then \
		$(MAKE) firmware-freertos; \
	fi

# Build all firmware targets (conditionally includes FreeRTOS if enabled)
firmware-all: firmware-bare firmware-newlib firmware-freertos-if-needed
	@echo ""
	@echo "========================================="
	@echo "✓ All firmware targets built"
	@echo "========================================="
	@echo ""
	@echo "Built firmware:"
	@ls -lh firmware/*.hex 2>/dev/null || echo "No firmware built yet"

upload-tool:
	@echo "========================================="
	@echo "Building Firmware Upload Tool"
	@echo "========================================="
	@$(MAKE) -C tools/uploader
	@echo ""
	@echo "✓ Upload tool built: tools/uploader/fw_upload"

# ============================================================================
# lwIP Performance Testing Tools
# ============================================================================

lwip-tools: slip-perf-client slip-perf-server
	@echo ""
	@echo "========================================="
	@echo "✓ lwIP tools built"
	@echo "========================================="
	@echo "  slip_perf_client:        tools/slip_perf_client/slip_perf_client"
	@echo "  slip_perf_server_linux:  tools/slip_perf_server_linux/slip_perf_server_linux"
	@echo ""

slip-perf-client:
	@echo "========================================="
	@echo "Building SLIP Performance Client"
	@echo "========================================="
	@$(MAKE) -C tools/slip_perf_client
	@echo ""
	@echo "✓ SLIP client built: tools/slip_perf_client/slip_perf_client"

slip-perf-server:
	@echo "========================================="
	@echo "Building SLIP Performance Server (Linux)"
	@echo "========================================="
	@$(MAKE) -C tools/slip_perf_server_linux
	@echo ""
	@echo "✓ SLIP server built: tools/slip_perf_server_linux/slip_perf_server_linux"

# ============================================================================
# HDL Synthesis and Bitstream Generation
# ============================================================================

bitstream: toolchain-if-needed bootloader synth pnr pack
	@echo ""
	@echo "========================================="
	@echo "✓ Bitstream generation complete"
	@echo "========================================="
	@echo "Bitstream: build/ice40_picorv32.bin"
	@ls -lh build/ice40_picorv32.bin
	@echo ""
	@echo "To program FPGA:"
	@echo "  iceprog build/ice40_picorv32.bin"

# Synthesis: Verilog -> JSON (requires bootloader.hex)
synth: bootloader
	@echo "========================================="
	@echo "Synthesis: Verilog -> JSON"
	@echo "========================================="
	@. ./.config && \
	SYNTH_OPTS=""; \
	if [ "$$CONFIG_SYNTH_ABC9" = "y" ]; then \
		SYNTH_OPTS="-abc9"; \
		echo "ABC9:    enabled"; \
	else \
		echo "ABC9:    disabled"; \
	fi; \
	echo "Tool:    Yosys"; \
	echo "Target:  iCE40HX8K"; \
	echo ""; \
	YOSYS_CMD="yosys"; \
	if [ -f $(CURDIR)/downloads/oss-cad-suite/bin/yosys ]; then \
		YOSYS_CMD="$(CURDIR)/downloads/oss-cad-suite/bin/yosys"; \
		echo "Using: $$YOSYS_CMD"; \
	fi; \
	$$YOSYS_CMD -p "synth_ice40 -top ice40_picorv32_top -json build/ice40_picorv32.json $$SYNTH_OPTS" \
		hdl/picorv32.v \
		hdl/uart.v \
		hdl/circular_buffer.v \
		hdl/crc32_gen.v \
		hdl/sram_controller_unified.v \
		hdl/sram_unified_adapter.v \
		hdl/firmware_loader.v \
		hdl/bootloader_rom.v \
		hdl/mem_controller.v \
		hdl/uart_peripheral.v \
		hdl/timer_peripheral.v \
		hdl/spi_master.v \
		hdl/ice40_picorv32_top.v
	@echo ""
	@echo "✓ Synthesis complete: build/ice40_picorv32.json"

# Place and Route: JSON -> ASC
pnr: synth
	@echo "========================================="
	@echo "Place and Route: JSON -> ASC"
	@echo "========================================="
	@. ./.config && \
	PCF_FILE="$$CONFIG_PCF_FILE"; \
	if [ -z "$$PCF_FILE" ]; then \
		PCF_FILE="hdl/ice40_picorv32.pcf"; \
	fi; \
	echo "Tool:    NextPNR-iCE40"; \
	echo "Device:  hx8k"; \
	echo "Package: ct256"; \
	echo "PCF:     $$PCF_FILE"; \
	echo "Placer:  heap --seed 1"; \
	echo ""; \
	NEXTPNR_CMD="nextpnr-ice40"; \
	if [ -f $(CURDIR)/downloads/oss-cad-suite/bin/nextpnr-ice40 ]; then \
		NEXTPNR_CMD="$(CURDIR)/downloads/oss-cad-suite/bin/nextpnr-ice40"; \
		echo "Using: $$NEXTPNR_CMD"; \
	fi; \
	$$NEXTPNR_CMD --hx8k --package ct256 \
		--json build/ice40_picorv32.json \
		--pcf "$$PCF_FILE" \
		--sdc hdl/ice40_picorv32.sdc \
		--asc build/ice40_picorv32.asc \
		--placer heap --seed 1
	@echo ""
	@echo "✓ Place and route complete: build/ice40_picorv32.asc"

# Alternative: Simulated Annealing placer (better for tight designs)
pnr-sa: synth
	@echo "========================================="
	@echo "Place and Route: JSON -> ASC (SA)"
	@echo "========================================="
	@. ./.config && \
	PCF_FILE="$$CONFIG_PCF_FILE"; \
	if [ -z "$$PCF_FILE" ]; then \
		PCF_FILE="hdl/ice40_picorv32.pcf"; \
	fi; \
	echo "Tool:    NextPNR-iCE40"; \
	echo "Device:  hx8k"; \
	echo "Package: ct256"; \
	echo "PCF:     $$PCF_FILE"; \
	echo "Placer:  SA (Simulated Annealing)"; \
	echo ""; \
	nextpnr-ice40 --hx8k --package ct256 \
		--json build/ice40_picorv32.json \
		--pcf "$$PCF_FILE" \
		--sdc hdl/ice40_picorv32.sdc \
		--asc build/ice40_picorv32.asc \
		--placer sa --ignore-loops
	@echo ""
	@echo "✓ Place and route complete: build/ice40_picorv32.asc"

# Alternative: Try multiple seeds (for tight designs at ~90% utilization)
pnr-seeds: synth
	@echo "========================================="
	@echo "Place and Route: Trying Multiple Seeds"
	@echo "========================================="
	@. ./.config && \
	PCF_FILE="$$CONFIG_PCF_FILE"; \
	if [ -z "$$PCF_FILE" ]; then \
		PCF_FILE="hdl/ice40_picorv32.pcf"; \
	fi; \
	echo "Tool:    NextPNR-iCE40"; \
	echo "Device:  hx8k"; \
	echo "Package: ct256"; \
	echo "PCF:     $$PCF_FILE"; \
	echo "Useful for nextpnr-0.9+ with 90%+ utilization"; \
	echo ""; \
	NEXTPNR_CMD="nextpnr-ice40"; \
	if [ -f $(CURDIR)/downloads/oss-cad-suite/bin/nextpnr-ice40 ]; then \
		NEXTPNR_CMD="$(CURDIR)/downloads/oss-cad-suite/bin/nextpnr-ice40"; \
		echo "Using: $$NEXTPNR_CMD"; \
	fi; \
	for seed in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do \
		echo "Trying seed $$seed..."; \
		if $$NEXTPNR_CMD --hx8k --package ct256 \
		   --json build/ice40_picorv32.json --pcf "$$PCF_FILE" \
		   --sdc hdl/ice40_picorv32.sdc \
		   --asc build/ice40_picorv32.asc --placer heap --seed $$seed; then \
			echo "✓ Success with seed $$seed!"; \
			break; \
		else \
			echo "✗ Seed $$seed failed, trying next..."; \
		fi; \
	done
	@if [ -f build/ice40_picorv32.asc ]; then \
		echo ""; \
		echo "✓ Place and route complete: build/ice40_picorv32.asc"; \
	else \
		echo ""; \
		echo "✗ All seeds failed - design may not fit"; \
		exit 1; \
	fi

# Pack Bitstream: ASC -> BIN
pack: pnr
	@echo "========================================="
	@echo "Pack Bitstream: ASC -> BIN"
	@echo "========================================="
	icepack build/ice40_picorv32.asc build/ice40_picorv32.bin
	@echo "✓ Bitstream packed: build/ice40_picorv32.bin"

# Timing analysis
timing: pnr
	@echo "========================================="
	@echo "Timing Analysis"
	@echo "========================================="
	icetime -d hx8k -mtr build/timing_report.txt build/ice40_picorv32.asc
	@echo ""
	@echo "Timing report:"
	@grep -A5 "Max frequency" build/timing_report.txt || cat build/timing_report.txt

# ============================================================================
# Artifacts Collection
# ============================================================================

artifacts:
	@echo "========================================="
	@echo "Collecting Build Artifacts"
	@echo "========================================="
	@echo ""
	@# Create directory structure
	@rm -rf artifacts
	@mkdir -p artifacts/host artifacts/gateware artifacts/firmware
	@echo "✓ Created artifacts directory structure"
	@echo ""
	@# Copy host tools
	@if [ -f tools/uploader/fw_upload ]; then \
		cp tools/uploader/fw_upload artifacts/host/; \
		echo "✓ Copied fw_upload to artifacts/host/"; \
	else \
		echo "⚠ fw_upload not found"; \
	fi
	@if [ -f tools/slip_perf_client/slip_perf_client ]; then \
		cp tools/slip_perf_client/slip_perf_client artifacts/host/; \
		echo "✓ Copied slip_perf_client to artifacts/host/"; \
	else \
		echo "⚠ slip_perf_client not found"; \
	fi
	@if [ -f tools/slip_perf_server_linux/slip_perf_server_linux ]; then \
		cp tools/slip_perf_server_linux/slip_perf_server_linux artifacts/host/; \
		echo "✓ Copied slip_perf_server_linux to artifacts/host/"; \
	else \
		echo "⚠ slip_perf_server_linux not found"; \
	fi
	@echo ""
	@# Copy gateware
	@if [ -f build/ice40_picorv32.bin ]; then \
		cp build/ice40_picorv32.bin artifacts/gateware/; \
		echo "✓ Copied bitstream to artifacts/gateware/"; \
	else \
		echo "⚠ Bitstream not found"; \
	fi
	@echo ""
	@# Copy firmware binaries (exclude broken FreeRTOS binaries)
	@if [ -n "$$(find firmware -maxdepth 1 -name '*.bin' ! -name 'freertos*.bin' 2>/dev/null)" ]; then \
		find firmware -maxdepth 1 -name "*.bin" ! -name "freertos*.bin" -exec cp {} artifacts/firmware/ \;; \
		echo "✓ Copied firmware binaries to artifacts/firmware/"; \
		find artifacts/firmware/ -maxdepth 1 -name "*.bin" -exec basename {} \; | sed 's/^/  - /'; \
	else \
		echo "⚠ No firmware binaries found"; \
	fi
	@# Copy overlay binaries from overlay_sdk projects
	@if [ -d firmware/overlay_sdk/projects ] && [ -n "$$(find firmware/overlay_sdk/projects -name '*.bin' 2>/dev/null)" ]; then \
		mkdir -p artifacts/firmware/overlays; \
		find firmware/overlay_sdk/projects -name "*.bin" -exec cp {} artifacts/firmware/overlays/ \;; \
		echo "✓ Copied overlay binaries to artifacts/firmware/overlays/"; \
		find artifacts/firmware/overlays/ -name "*.bin" -exec basename {} \; | sed 's/^/  - /'; \
	else \
		echo "⚠ No overlay binaries found (run 'make firmware-overlays' to build them)"; \
	fi
	@# Copy SDCARD/FatFS binaries if they exist
	@if [ -d firmware/sd_fatfs ] && [ -n "$$(find firmware/sd_fatfs -name '*.bin' 2>/dev/null)" ]; then \
		mkdir -p artifacts/firmware/sd_fatfs; \
		find firmware/sd_fatfs -name "*.bin" -exec cp {} artifacts/firmware/sd_fatfs/ \;; \
		echo "✓ Copied SDCARD binaries to artifacts/firmware/sd_fatfs/"; \
		find artifacts/firmware/sd_fatfs/ -name "*.bin" -exec basename {} \; | sed 's/^/  - /'; \
	fi
	@echo ""
	@# Generate build report
	@echo "Generating build report..."
	@echo "==========================================" > artifacts/build-report.txt
	@echo "Olimex iCE40HX8K PicoRV32 Build Report" >> artifacts/build-report.txt
	@echo "==========================================" >> artifacts/build-report.txt
	@echo "" >> artifacts/build-report.txt
	@echo "Build Timestamp: $$(date)" >> artifacts/build-report.txt
	@echo "Build Host: $$(hostname)" >> artifacts/build-report.txt
	@echo "Build Platform: $$(uname -s) $$(uname -r)" >> artifacts/build-report.txt
	@echo "Architecture: $$(uname -m)" >> artifacts/build-report.txt
	@echo "CPU Cores: $$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 'Unknown')" >> artifacts/build-report.txt
	@echo "Total RAM: $$(free -h 2>/dev/null | awk '/^Mem:/ {print $$2}' || sysctl -n hw.memsize 2>/dev/null | awk '{printf "%.1fG", $$1/1024/1024/1024}' || echo 'Unknown')" >> artifacts/build-report.txt
	@echo "" >> artifacts/build-report.txt
	@echo "==========================================" >> artifacts/build-report.txt
	@echo "⚠️  NOT FOR COMMERCIAL USE ⚠️" >> artifacts/build-report.txt
	@echo "EDUCATIONAL AND RESEARCH PURPOSES ONLY" >> artifacts/build-report.txt
	@echo "==========================================" >> artifacts/build-report.txt
	@echo "" >> artifacts/build-report.txt
	@echo "Copyright (c) October 2025 Michael Wolak" >> artifacts/build-report.txt
	@echo "Email: mikewolak@gmail.com, mike@epromfoundry.com" >> artifacts/build-report.txt
	@echo "" >> artifacts/build-report.txt
	@echo "==========================================" >> artifacts/build-report.txt
	@echo "Tool Versions" >> artifacts/build-report.txt
	@echo "==========================================" >> artifacts/build-report.txt
	@echo "" >> artifacts/build-report.txt
	@# Detect which Yosys to use
	@if [ -f downloads/oss-cad-suite/bin/yosys ]; then \
		echo "Yosys: $$(downloads/oss-cad-suite/bin/yosys --version 2>&1 | head -1)" >> artifacts/build-report.txt; \
	elif command -v yosys >/dev/null 2>&1; then \
		echo "Yosys: $$(yosys --version 2>&1 | head -1)" >> artifacts/build-report.txt; \
	else \
		echo "Yosys: Not found" >> artifacts/build-report.txt; \
	fi
	@# Detect which NextPNR to use
	@if [ -f downloads/oss-cad-suite/bin/nextpnr-ice40 ]; then \
		echo "NextPNR: $$(downloads/oss-cad-suite/bin/nextpnr-ice40 --version 2>&1 | head -1)" >> artifacts/build-report.txt; \
	elif command -v nextpnr-ice40 >/dev/null 2>&1; then \
		echo "NextPNR: $$(nextpnr-ice40 --version 2>&1 | head -1)" >> artifacts/build-report.txt; \
	else \
		echo "NextPNR: Not found" >> artifacts/build-report.txt; \
	fi
	@# Detect which icetime to use (icetime doesn't have --version, just check if it exists)
	@if [ -f downloads/oss-cad-suite/bin/icetime ]; then \
		echo "IceTime: Found (from oss-cad-suite)" >> artifacts/build-report.txt; \
	elif command -v icetime >/dev/null 2>&1; then \
		echo "IceTime: Found (system)" >> artifacts/build-report.txt; \
	else \
		echo "IceTime: Not found" >> artifacts/build-report.txt; \
	fi
	@# RISC-V GCC version
	@if [ -f build/toolchain/bin/riscv64-unknown-elf-gcc ]; then \
		echo "RISC-V GCC: $$(build/toolchain/bin/riscv64-unknown-elf-gcc --version 2>&1 | head -1)" >> artifacts/build-report.txt; \
	elif [ -f build/toolchain/bin/riscv32-unknown-elf-gcc ]; then \
		echo "RISC-V GCC: $$(build/toolchain/bin/riscv32-unknown-elf-gcc --version 2>&1 | head -1)" >> artifacts/build-report.txt; \
	elif command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then \
		echo "RISC-V GCC: $$(riscv64-unknown-elf-gcc --version 2>&1 | head -1)" >> artifacts/build-report.txt; \
	else \
		echo "RISC-V GCC: Not found" >> artifacts/build-report.txt; \
	fi
	@echo "" >> artifacts/build-report.txt
	@echo "==========================================" >> artifacts/build-report.txt
	@echo "FPGA Utilization" >> artifacts/build-report.txt
	@echo "==========================================" >> artifacts/build-report.txt
	@echo "" >> artifacts/build-report.txt
	@if [ -f build/ice40_picorv32.asc ]; then \
		if [ -f downloads/oss-cad-suite/bin/icebox_stat ]; then \
			downloads/oss-cad-suite/bin/icebox_stat build/ice40_picorv32.asc >> artifacts/build-report.txt 2>&1; \
		elif command -v icebox_stat >/dev/null 2>&1; then \
			icebox_stat build/ice40_picorv32.asc >> artifacts/build-report.txt 2>&1; \
		else \
			echo "Utilization data not available (icebox_stat not found)" >> artifacts/build-report.txt; \
		fi; \
	else \
		echo "Utilization data not available (build/ice40_picorv32.asc not found)" >> artifacts/build-report.txt; \
	fi
	@echo "" >> artifacts/build-report.txt
	@echo "==========================================" >> artifacts/build-report.txt
	@echo "Timing Analysis" >> artifacts/build-report.txt
	@echo "==========================================" >> artifacts/build-report.txt
	@echo "" >> artifacts/build-report.txt
	@if [ -f build/timing_report.txt ]; then \
		cat build/timing_report.txt >> artifacts/build-report.txt; \
	else \
		echo "Timing report not available" >> artifacts/build-report.txt; \
	fi
	@echo "" >> artifacts/build-report.txt
	@echo "==========================================" >> artifacts/build-report.txt
	@echo "Build Artifacts Tree" >> artifacts/build-report.txt
	@echo "==========================================" >> artifacts/build-report.txt
	@echo "" >> artifacts/build-report.txt
	@if command -v tree >/dev/null 2>&1; then \
		tree artifacts >> artifacts/build-report.txt; \
	else \
		find artifacts -type f -o -type d | sort | sed 's|^artifacts|.|' >> artifacts/build-report.txt; \
	fi
	@echo "" >> artifacts/build-report.txt
	@echo "✓ Build report generated: artifacts/build-report.txt"
	@echo ""
	@# Create tar.gz archive with version and date
	@GIT_TAG=$$(git describe --tags --always 2>/dev/null || echo "0.1-initial"); \
	BUILD_DATE=$$(date +%Y%m%d-%H%M%S); \
	ARCHIVE_NAME="olimex-ice40hx8k-picorv32-$${GIT_TAG}-$${BUILD_DATE}"; \
	echo "Creating release archive: $${ARCHIVE_NAME}.tar.gz"; \
	tar -czf artifacts/$${ARCHIVE_NAME}.tar.gz -C artifacts host gateware firmware build-report.txt 2>/dev/null || tar -czf artifacts/$${ARCHIVE_NAME}.tar.gz artifacts/host artifacts/gateware artifacts/firmware artifacts/build-report.txt; \
	echo "✓ Release archive created: artifacts/$${ARCHIVE_NAME}.tar.gz"; \
	ls -lh artifacts/$${ARCHIVE_NAME}.tar.gz
	@echo ""
	@echo "========================================="
	@echo "✓ Artifacts Collection Complete"
	@echo "========================================="

# ============================================================================
# Clean targets
# ============================================================================

clean:
	@echo "Cleaning build artifacts..."
	@rm -rf build/ deploy/ artifacts/
	@echo "✓ Clean complete"

distclean: clean
	@echo "Cleaning configuration..."
	@rm -f .config .config.old build.ninja .ninja_*
	@echo "✓ Configuration cleaned"

mrproper: distclean
	@echo "Mr. Proper: Removing all downloaded dependencies..."
	@rm -rf downloads/
	@echo "✓ Repository pristine"

# Check for .config
.config:
	@echo "ERROR: No .config found"
	@echo "Run 'make defconfig' or 'make menuconfig' first"
	@exit 1
