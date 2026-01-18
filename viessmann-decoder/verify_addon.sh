#!/bin/bash
set -e

# Viessmann Decoder Add-on Verification Script
# This script performs comprehensive verification of the add-on

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_section() {
    echo -e "\n${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}"
}

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}⚠${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

print_section "VIESSMANN DECODER ADD-ON VERIFICATION"

# Check if we're in the addon directory
if [ ! -f "config.yaml" ]; then
    print_error "Must be run from the viessmann-decoder addon directory"
    exit 1
fi

print_section "1. FILE STRUCTURE VERIFICATION"

# Check required files
required_files=(
    "config.yaml"
    "build.json"
    "Dockerfile"
    "run.sh"
    "apparmor.txt"
    "README.md"
    "DOCS.md"
    "CHANGELOG.md"
    "rootfs/etc/services.d/viessmann-decoder/run"
    "src/vbusdecoder.h"
    "src/vbusdecoder.cpp"
    "src/VBUSDataLogger.h"
    "src/VBUSDataLogger.cpp"
    "webserver/main.cpp"
    "linux/include/Arduino.h"
    "linux/include/LinuxSerial.h"
    "linux/src/Arduino.cpp"
    "linux/src/LinuxSerial.cpp"
)

for file in "${required_files[@]}"; do
    if [ -f "$file" ]; then
        print_success "Found: $file"
    else
        print_error "Missing: $file"
    fi
done

print_section "2. CONFIGURATION VALIDATION"

# Check config.yaml syntax
if python3 -c "import yaml; yaml.safe_load(open('config.yaml'))" 2>/dev/null; then
    print_success "config.yaml syntax is valid"
else
    print_error "config.yaml has syntax errors"
fi

# Check build.json syntax  
if python3 -c "import json; json.load(open('build.json'))" 2>/dev/null; then
    print_success "build.json syntax is valid"
else
    print_error "build.json has syntax errors"
fi

# Check executable permissions
if [ -x "run.sh" ]; then
    print_success "run.sh is executable"
else
    print_warning "run.sh might need executable permissions"
fi

if [ -x "rootfs/etc/services.d/viessmann-decoder/run" ]; then
    print_success "service run script is executable"
else
    print_warning "service run script might need executable permissions"
fi

print_section "3. ARCHITECTURE SUPPORT VERIFICATION"

# Check supported architectures in config vs build.json
config_archs=$(python3 -c "import yaml; print(' '.join(yaml.safe_load(open('config.yaml'))['arch']))" 2>/dev/null || echo "")
build_archs=$(python3 -c "import json; print(' '.join(json.load(open('build.json'))['build_from'].keys()))" 2>/dev/null || echo "")

echo "Architectures in config.yaml: $config_archs"
echo "Architectures in build.json: $build_archs"

if [ "$config_archs" = "$build_archs" ]; then
    print_success "Architecture definitions match"
else
    print_warning "Architecture mismatch between config.yaml and build.json"
fi

print_section "4. SOURCE CODE VERIFICATION"

# Check C++ source files for basic issues
cpp_files=("src/vbusdecoder.cpp" "webserver/main.cpp" "linux/src/Arduino.cpp" "linux/src/LinuxSerial.cpp")
for file in "${cpp_files[@]}"; do
    if [ -f "$file" ]; then
        # Check for basic C++ syntax issues
        if grep -q "#include" "$file" && grep -q ";" "$file"; then
            print_success "$file: Basic syntax looks good"
        else
            print_warning "$file: May have syntax issues"
        fi
        
        # Check file size (should not be empty)
        if [ -s "$file" ]; then
            print_success "$file: Non-empty file"
        else
            print_error "$file: File is empty"
        fi
    fi
done

print_section "5. DEPENDENCY VERIFICATION"

# Check Dockerfile dependencies
if grep -q "libmicrohttpd" Dockerfile; then
    print_success "Dockerfile includes libmicrohttpd dependency"
else
    print_error "Missing libmicrohttpd dependency in Dockerfile"
fi

if grep -q "build-base" Dockerfile; then
    print_success "Dockerfile includes build dependencies"
else
    print_error "Missing build dependencies in Dockerfile"
fi

print_section "6. SECURITY CONFIGURATION"

# Check AppArmor profile
if grep -q "capability sys_rawio" apparmor.txt; then
    print_success "AppArmor profile includes sys_rawio capability"
else
    print_error "Missing sys_rawio capability in AppArmor profile"
fi

if grep -q "/dev/tty" apparmor.txt; then
    print_success "AppArmor profile allows serial device access"
else
    print_error "Missing serial device access in AppArmor profile"
fi

# Check config.yaml for required permissions
if grep -q "SYS_RAWIO" config.yaml; then
    print_success "config.yaml includes SYS_RAWIO privilege"
else
    print_error "Missing SYS_RAWIO privilege in config.yaml"
fi

if grep -q "uart: true" config.yaml; then
    print_success "config.yaml enables UART access"
else
    print_error "Missing UART access in config.yaml"
fi

print_section "7. INTEGRATION VERIFICATION"

# Check Home Assistant integration settings
if grep -q "ingress: true" config.yaml; then
    print_success "Home Assistant ingress enabled"
else
    print_warning "Ingress not enabled - web interface won't be embedded"
fi

if grep -q "watchdog:" config.yaml; then
    print_success "Health check watchdog configured"
else
    print_warning "No health check watchdog configured"
fi

print_section "8. BUILD VERIFICATION SUMMARY"

echo ""
echo "Verification completed. Check any warnings or errors above."
echo ""
echo "If all checks passed, the add-on should build and run correctly."
echo "To test the build:"
echo "  ./build.sh amd64"
echo ""
echo "To install in Home Assistant:"
echo "  1. Add the repository to Home Assistant"
echo "  2. Install the Viessmann Decoder add-on"
echo "  3. Configure serial port and protocol settings"
echo "  4. Start the add-on"
echo ""