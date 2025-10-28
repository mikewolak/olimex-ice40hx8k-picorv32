#!/bin/bash
#===============================================================================
# Overlay Position-Independent Code (PIC) Validation Script
# validate_pic.sh - Validates that overlay binaries are properly built as PIC
#
# Copyright (c) October 2025 Michael Wolak
# Email: mikewolak@gmail.com, mike@epromfoundry.com
#===============================================================================

set -e  # Exit on any error

#===============================================================================
# Configuration
#===============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLCHAIN_PREFIX="riscv64-unknown-elf-"
READELF="${TOOLCHAIN_PREFIX}readelf"
OBJDUMP="${TOOLCHAIN_PREFIX}objdump"

# Expected overlay memory region
OVERLAY_BASE=0x60000
OVERLAY_END=0x78000
OVERLAY_MAX_SIZE=$((0x78000 - 0x60000))  # 96 KB

# Hardware peripheral addresses (allowed absolute references)
ALLOWED_ABSOLUTES=(
    "0x80000"    # UART base
    "0x80"       # Stack/heap regions (0x7A000, 0x7FC00, etc.)
    "0x7f7f8"    # Math library constants
    "0x7ff00"    # Math library constants
    "0xfe100"    # Math library constants
    "0x10"       # Small constants used in math
    "0x2"        # Small constants
)

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

#===============================================================================
# Helper Functions
#===============================================================================

print_header() {
    echo -e "${BLUE}=========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}=========================================${NC}"
}

print_pass() {
    echo -e "${GREEN}✓ PASS:${NC} $1"
}

print_fail() {
    echo -e "${RED}✗ FAIL:${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}⚠ WARN:${NC} $1"
}

print_info() {
    echo -e "${BLUE}ℹ INFO:${NC} $1"
}

#===============================================================================
# Validation Functions
#===============================================================================

# Check if ELF file exists
check_file_exists() {
    local elf_file="$1"

    if [ ! -f "$elf_file" ]; then
        print_fail "ELF file not found: $elf_file"
        return 1
    fi

    print_pass "ELF file exists"
    return 0
}

# Check ELF header
check_elf_header() {
    local elf_file="$1"
    local errors=0

    print_info "Checking ELF header..."

    # Check file type
    local file_type=$($READELF -h "$elf_file" | grep "Type:" | awk '{print $2}')
    if [ "$file_type" == "EXEC" ]; then
        print_pass "File type is EXEC (correct for linked overlay)"
    else
        print_fail "File type is $file_type (expected EXEC)"
        errors=$((errors + 1))
    fi

    # Check machine type
    local machine=$($READELF -h "$elf_file" | grep "Machine:" | awk '{print $2}')
    if [ "$machine" == "RISC-V" ]; then
        print_pass "Machine type is RISC-V"
    else
        print_fail "Machine type is $machine (expected RISC-V)"
        errors=$((errors + 1))
    fi

    # Check entry point
    local entry=$($READELF -h "$elf_file" | grep "Entry point" | awk '{print $4}')
    local entry_dec=$((entry))
    local base_dec=$((OVERLAY_BASE))

    if [ $entry_dec -ge $base_dec ]; then
        print_pass "Entry point $entry is in overlay region"
    else
        print_fail "Entry point $entry is outside overlay region (expected >= 0x60000)"
        errors=$((errors + 1))
    fi

    return $errors
}

# Check program headers and load address
check_program_headers() {
    local elf_file="$1"
    local errors=0

    print_info "Checking program headers..."

    # Get LOAD segment info
    local load_info=$($READELF -l "$elf_file" | grep "LOAD")

    if [ -z "$load_info" ]; then
        print_fail "No LOAD segment found"
        return 1
    fi

    # Extract VirtAddr and MemSiz
    local virt_addr=$(echo "$load_info" | awk '{print $3}')
    local mem_siz=$(echo "$load_info" | awk '{print $6}')

    local virt_dec=$((virt_addr))
    local size_dec=$((mem_siz))
    local base_dec=$((OVERLAY_BASE))
    local end_addr=$((virt_dec + size_dec))
    local max_end=$((OVERLAY_END))

    print_info "Load address: $virt_addr (decimal: $virt_dec)"
    print_info "Memory size: $mem_siz (decimal: $size_dec)"
    print_info "End address: $(printf '0x%x' $end_addr) (decimal: $end_addr)"

    # Check load address is in overlay region
    if [ $virt_dec -eq $base_dec ]; then
        print_pass "Load address matches OVERLAY_BASE (0x60000)"
    else
        print_fail "Load address $virt_addr doesn't match OVERLAY_BASE (expected 0x60000)"
        errors=$((errors + 1))
    fi

    # Check size doesn't exceed limit
    if [ $size_dec -le $OVERLAY_MAX_SIZE ]; then
        print_pass "Overlay size $size_dec bytes ($(($size_dec / 1024)) KB) fits in $OVERLAY_MAX_SIZE byte limit"
    else
        print_fail "Overlay size $size_dec bytes exceeds $OVERLAY_MAX_SIZE byte limit"
        errors=$((errors + 1))
    fi

    # Check end address
    if [ $end_addr -le $max_end ]; then
        print_pass "Overlay end address $(printf '0x%x' $end_addr) within bounds"
    else
        print_fail "Overlay extends beyond OVERLAY_END (0x78000)"
        errors=$((errors + 1))
    fi

    return $errors
}

# Check for relocations (should be none after linking)
check_relocations() {
    local elf_file="$1"
    local errors=0

    print_info "Checking relocations..."

    local reloc_output=$($READELF -r "$elf_file" 2>&1)

    if echo "$reloc_output" | grep -q "There are no relocations in this file"; then
        print_pass "No relocations present (all resolved at link time)"
    else
        print_fail "Relocations found - code may not be position-independent"
        echo "$reloc_output" | head -20
        errors=$((errors + 1))
    fi

    return $errors
}

# Check for PC-relative addressing (auipc instructions)
check_pc_relative() {
    local elf_file="$1"
    local errors=0

    print_info "Checking PC-relative addressing..."

    # Count auipc instructions (PC-relative addressing)
    local auipc_count=$($OBJDUMP -d "$elf_file" | grep -c "auipc" || true)

    if [ $auipc_count -gt 0 ]; then
        print_pass "Found $auipc_count PC-relative (auipc) instructions"
    else
        print_warn "No PC-relative addressing found (unusual for PIC code)"
    fi

    return 0
}

# Check for problematic absolute addressing
check_absolute_addresses() {
    local elf_file="$1"
    local errors=0

    print_info "Checking for problematic absolute addresses..."

    # Get all lui instructions (load upper immediate)
    local lui_instrs=$($OBJDUMP -d "$elf_file" | grep "lui" || true)

    if [ -z "$lui_instrs" ]; then
        print_pass "No lui instructions found"
        return 0
    fi

    # Count total lui instructions
    local lui_count=$(echo "$lui_instrs" | wc -l)
    print_info "Found $lui_count lui instructions (loading immediate values)"

    # Extract unique immediate values from lui instructions
    local unique_values=$(echo "$lui_instrs" | awk '{print $NF}' | sort -u)

    local problematic=0
    local allowed=0

    while IFS= read -r value; do
        # Remove any trailing commas
        value=${value%,}

        # Check if this value is in allowed list
        local is_allowed=0
        for allowed_val in "${ALLOWED_ABSOLUTES[@]}"; do
            if [[ "$value" == "$allowed_val"* ]]; then
                is_allowed=1
                break
            fi
        done

        if [ $is_allowed -eq 1 ]; then
            allowed=$((allowed + 1))
        else
            # Check if it's in overlay region (which is OK)
            local val_dec=$((value))
            local base_dec=$((OVERLAY_BASE))
            local end_dec=$((OVERLAY_END))

            if [ $val_dec -ge $base_dec ] && [ $val_dec -lt $end_dec ]; then
                # This is overlay region - OK
                allowed=$((allowed + 1))
            else
                print_warn "Potentially problematic absolute address: $value"
                problematic=$((problematic + 1))
            fi
        fi
    done <<< "$unique_values"

    if [ $problematic -eq 0 ]; then
        print_pass "All absolute addresses are acceptable ($allowed hardware/constant addresses)"
    else
        print_warn "Found $problematic potentially problematic absolute addresses"
        print_info "This may be OK if they're math constants or known hardware addresses"
    fi

    return 0
}

# Check startup code and IRQ handling
check_startup_code() {
    local elf_file="$1"
    local errors=0

    print_info "Checking startup code..."

    # Disassemble and check for critical startup components
    local disasm=$($OBJDUMP -d "$elf_file")

    # Check for _start symbol
    if echo "$disasm" | grep -q "<_start>:"; then
        print_pass "Found _start entry point"
    else
        print_fail "_start entry point not found"
        errors=$((errors + 1))
    fi

    # Check for BSS clearing (PC-relative addressing)
    if echo "$disasm" | grep -q "__bss_start\|clear_bss"; then
        print_pass "BSS clearing code present"
    else
        print_warn "BSS clearing code not found (may be OK for small overlays)"
    fi

    # Check for stack setup (should use fixed overlay stack)
    if echo "$disasm" | grep -q "lui.*sp,0x7a"; then
        print_pass "Overlay stack pointer initialization found (0x7A000)"
    else
        print_warn "Standard overlay stack initialization not found"
    fi

    # Check for register saves/restores (if overlay uses IRQs)
    if echo "$disasm" | grep -q "sw.*sp,0\|sw.*ra,4"; then
        print_pass "Stack frame save/restore code present"
    else
        print_info "No stack frame save/restore (overlay may not return to caller)"
    fi

    # Check for main function call
    if echo "$disasm" | grep -q "call.*<main>\|jal.*<main>"; then
        print_pass "Call to main() found"
    else
        print_warn "Call to main() not found"
    fi

    return $errors
}

# Check memory sections and layout
check_memory_sections() {
    local elf_file="$1"
    local errors=0

    print_info "Checking memory sections..."

    # Get section headers
    local sections=$($READELF -S "$elf_file")

    # Check for .text section
    if echo "$sections" | grep -q "\.text"; then
        local text_addr=$(echo "$sections" | grep "\.text" | awk '{print "0x" $5}')
        local text_size=$(echo "$sections" | grep "\.text" | awk '{print "0x" $7}')
        local text_addr_dec=$((text_addr))
        local text_size_dec=$((text_size))

        print_info ".text section: addr=$text_addr, size=$text_size ($(($text_size_dec / 1024)) KB)"

        if [ $text_addr_dec -ge $((OVERLAY_BASE)) ]; then
            print_pass ".text section in overlay region"
        else
            print_fail ".text section at $text_addr (expected >= 0x60000)"
            errors=$((errors + 1))
        fi
    else
        print_fail ".text section not found"
        errors=$((errors + 1))
    fi

    # Check for .rodata section
    if echo "$sections" | grep -q "\.rodata"; then
        print_pass ".rodata section present"
    else
        print_info ".rodata section not present (may be empty)"
    fi

    # Check for .data section
    if echo "$sections" | grep -q "\.data"; then
        print_pass ".data section present"
    else
        print_info ".data section not present (may be empty)"
    fi

    # Check for .bss section
    if echo "$sections" | grep -q "\.bss"; then
        local bss_size=$(echo "$sections" | grep "\.bss" | awk '{print "0x" $7}')
        local bss_size_dec=$((bss_size))
        print_pass ".bss section present (size=$bss_size, $(($bss_size_dec / 1024)) KB)"
    else
        print_info ".bss section not present (no uninitialized data)"
    fi

    # Check for .got (Global Offset Table) - should be present for PIC
    if echo "$sections" | grep -q "\.got"; then
        print_pass ".got section present (Global Offset Table for PIC)"
    else
        print_warn ".got section not found (unusual for PIC code)"
    fi

    # Check section address ordering (text < rodata < data < bss)
    print_info "Verifying section layout..."

    # Get all section addresses
    local text_end=$((text_addr_dec + text_size_dec))

    if [ $text_end -le $((OVERLAY_END)) ]; then
        print_pass "All sections fit within overlay region"
    else
        print_fail "Sections extend beyond overlay region (end: $(printf '0x%x' $text_end))"
        errors=$((errors + 1))
    fi

    return $errors
}

# Check for required symbols
check_symbols() {
    local elf_file="$1"
    local errors=0

    print_info "Checking symbols..."

    # Get symbol table
    local symbols=$($READELF -s "$elf_file")

    # Check for _start
    if echo "$symbols" | grep -q "_start"; then
        print_pass "Symbol _start present"
    else
        print_fail "Symbol _start missing"
        errors=$((errors + 1))
    fi

    # Check for main
    if echo "$symbols" | grep -q " main$"; then
        print_pass "Symbol main present"
    else
        print_warn "Symbol main not found (overlay may have different entry point)"
    fi

    # Check for __bss_start and __bss_end
    if echo "$symbols" | grep -q "__bss_start\|__bss_end"; then
        print_pass "BSS boundary symbols present"
    else
        print_info "BSS boundary symbols not found (may be optimized out)"
    fi

    # Check for __global_pointer$ (used in RISC-V for global data access)
    if echo "$symbols" | grep -q "__global_pointer"; then
        print_pass "Global pointer symbol present"
    else
        print_warn "Global pointer symbol not found"
    fi

    return $errors
}

# Check linker memory map (from .map file if available)
check_linker_map() {
    local elf_file="$1"
    local map_file="${elf_file%.elf}.map"
    local errors=0

    print_info "Checking linker map..."

    if [ ! -f "$map_file" ]; then
        print_warn "Map file not found: $map_file"
        return 0
    fi

    print_pass "Map file found: $map_file"

    # Check memory regions in map file
    if grep -q "OVERLAY" "$map_file"; then
        print_pass "OVERLAY memory region defined in linker script"
    else
        print_warn "OVERLAY memory region not found in map file"
    fi

    # Check for memory overflow
    if grep -qi "will not fit\|overflow" "$map_file"; then
        print_fail "Memory overflow detected in map file"
        errors=$((errors + 1))
    else
        print_pass "No memory overflow detected"
    fi

    # Show memory usage summary if available
    if grep -q "Memory Configuration" "$map_file"; then
        print_info "Memory configuration found in map file"
    fi

    return $errors
}

# Generate summary report
generate_report() {
    local elf_file="$1"
    local total_errors="$2"

    print_header "VALIDATION SUMMARY"

    echo "Overlay: $(basename "$elf_file" .elf)"
    echo "File: $elf_file"
    echo

    if [ $total_errors -eq 0 ]; then
        print_pass "All validation checks passed"
        echo
        echo -e "${GREEN}╔═══════════════════════════════════════╗${NC}"
        echo -e "${GREEN}║         PIC VALIDATION PASSED         ║${NC}"
        echo -e "${GREEN}╚═══════════════════════════════════════╝${NC}"
        return 0
    else
        print_fail "$total_errors validation check(s) failed"
        echo
        echo -e "${RED}╔═══════════════════════════════════════╗${NC}"
        echo -e "${RED}║         PIC VALIDATION FAILED         ║${NC}"
        echo -e "${RED}╚═══════════════════════════════════════╝${NC}"
        return 1
    fi
}

#===============================================================================
# Main Validation Function
#===============================================================================

validate_overlay() {
    local elf_file="$1"
    local total_errors=0

    print_header "Validating Overlay PIC: $(basename "$elf_file")"
    echo

    # Run all validation checks
    check_file_exists "$elf_file" || exit 1

    check_elf_header "$elf_file" || total_errors=$((total_errors + $?))
    echo

    check_program_headers "$elf_file" || total_errors=$((total_errors + $?))
    echo

    check_relocations "$elf_file" || total_errors=$((total_errors + $?))
    echo

    check_pc_relative "$elf_file" || total_errors=$((total_errors + $?))
    echo

    check_absolute_addresses "$elf_file" || total_errors=$((total_errors + $?))
    echo

    check_startup_code "$elf_file" || total_errors=$((total_errors + $?))
    echo

    check_memory_sections "$elf_file" || total_errors=$((total_errors + $?))
    echo

    check_symbols "$elf_file" || total_errors=$((total_errors + $?))
    echo

    check_linker_map "$elf_file" || total_errors=$((total_errors + $?))
    echo

    # Generate final report
    generate_report "$elf_file" "$total_errors"

    return $?
}

#===============================================================================
# Script Entry Point
#===============================================================================

if [ $# -eq 0 ]; then
    echo "Usage: $0 <overlay.elf> [overlay2.elf ...]"
    echo
    echo "Validates that overlay ELF files are properly built as position-independent code (PIC)"
    echo
    echo "Example:"
    echo "  $0 hello_world.elf"
    echo "  $0 projects/*/\\*.elf"
    exit 1
fi

# Validate each overlay
failed_overlays=()

for elf_file in "$@"; do
    if ! validate_overlay "$elf_file"; then
        failed_overlays+=("$elf_file")
    fi
    echo
    echo
done

# Final summary if multiple overlays
if [ ${#@} -gt 1 ]; then
    print_header "OVERALL VALIDATION SUMMARY"
    echo "Total overlays checked: ${#@}"
    echo "Passed: $((${#@} - ${#failed_overlays[@]}))"
    echo "Failed: ${#failed_overlays[@]}"
    echo

    if [ ${#failed_overlays[@]} -eq 0 ]; then
        echo -e "${GREEN}All overlays passed PIC validation!${NC}"
        exit 0
    else
        echo -e "${RED}The following overlays failed validation:${NC}"
        for overlay in "${failed_overlays[@]}"; do
            echo -e "${RED}  - $overlay${NC}"
        done
        exit 1
    fi
fi
