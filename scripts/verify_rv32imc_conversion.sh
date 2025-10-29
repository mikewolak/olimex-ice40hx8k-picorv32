#!/bin/bash
# Verify RV32IMC conversion is complete

echo "========================================="
echo "RV32IMC Conversion Verification"
echo "========================================="
echo ""

ERRORS=0

# Check primary Makefiles
echo "1. Checking primary Makefiles..."
echo "-----------------------------------"

check_makefile() {
    local file=$1
    local line=$2
    if grep -q "^ARCH = rv32imc" "$file"; then
        echo "✅ $file - ARCH = rv32imc"
    else
        echo "❌ $file - MISSING or INCORRECT"
        ERRORS=$((ERRORS + 1))
    fi
}

check_makefile "bootloader/Makefile"
check_makefile "firmware/Makefile"
check_makefile "firmware/overlay_sdk/Makefile.overlay"
check_makefile "firmware/overlays/Makefile"

echo ""

# Check for any remaining rv32im (without c)
echo "2. Checking for remaining rv32im (without c)..."
echo "------------------------------------------------"
if grep -r "ARCH.*rv32im[^c]" bootloader/ firmware/ 2>/dev/null | grep -v ".git"; then
    echo "❌ Found rv32im without c!"
    ERRORS=$((ERRORS + 1))
else
    echo "✅ No rv32im found - all use rv32imc"
fi

echo ""

# Check build scripts
echo "3. Checking build scripts handle CONFIG_COMPRESSED_ISA..."
echo "----------------------------------------------------------"

check_script() {
    local file=$1
    if grep -q 'CONFIG_COMPRESSED_ISA.*"y"' "$file" && grep -q 'ARCH.*c' "$file"; then
        echo "✅ $file"
    else
        echo "❌ $file - doesn't handle CONFIG_COMPRESSED_ISA"
        ERRORS=$((ERRORS + 1))
    fi
}

check_script "scripts/build_newlib.sh"
check_script "scripts/build_newlib_pic.sh"
check_script "scripts/build_overlay_libs.sh"
check_script "scripts/build_firmware.sh"

echo ""

# Check HDL configuration
echo "4. Checking HDL configuration..."
echo "---------------------------------"
if grep -q "\.COMPRESSED_ISA(1)" hdl/ice40_picorv32_top.v; then
    echo "✅ PicoRV32 COMPRESSED_ISA enabled"
else
    echo "❌ PicoRV32 COMPRESSED_ISA not enabled"
    ERRORS=$((ERRORS + 1))
fi

if grep -q "\.CATCH_MISALIGN(1)" hdl/ice40_picorv32_top.v; then
    echo "✅ PicoRV32 CATCH_MISALIGN enabled"
else
    echo "⚠️  PicoRV32 CATCH_MISALIGN not enabled (should be for compressed ISA)"
fi

echo ""

# Check Kconfig
echo "5. Checking Kconfig..."
echo "----------------------"
if grep -A2 "config COMPRESSED_ISA" Kconfig | grep -q "default y"; then
    echo "✅ Kconfig COMPRESSED_ISA default = y"
else
    echo "❌ Kconfig COMPRESSED_ISA not default y"
    ERRORS=$((ERRORS + 1))
fi

echo ""

# Summary
echo "========================================="
if [ $ERRORS -eq 0 ]; then
    echo "✅ ALL CHECKS PASSED!"
    echo "========================================="
    echo ""
    echo "RV32IMC conversion is COMPLETE."
    echo ""
    echo "Next steps:"
    echo "  1. make clean (in bootloader and firmware)"
    echo "  2. Rebuild bootloader: cd bootloader && make"
    echo "  3. Rebuild firmware: cd firmware && make firmware"
    echo "  4. Compare binary sizes (expect 25-30% reduction)"
    exit 0
else
    echo "❌ FOUND $ERRORS ERROR(S)"
    echo "========================================="
    echo ""
    echo "Please fix the errors above before proceeding."
    exit 1
fi
