#!/bin/bash
# Comprehensive Viessmann Decoder Add-on Validation Script
# Performs thorough testing of all addon components before deployment

set -euo pipefail

# Configuration
readonly ADDON_DIR="/workspaces/developers.home-assistant/Viessmann-Home-Assistant-Addon-/viessmann-decoder"
readonly LOG_FILE="${ADDON_DIR}/validation_results.log"
readonly SUCCESS_COLOR="\033[0;32m"
readonly ERROR_COLOR="\033[0;31m"
readonly WARNING_COLOR="\033[1;33m"
readonly INFO_COLOR="\033[0;34m"
readonly NC="\033[0m"

# Counters
TOTAL_CHECKS=0
PASSED_CHECKS=0
FAILED_CHECKS=0
WARNINGS=0

# Logging functions
log_info() {
    echo -e "${INFO_COLOR}[INFO]${NC} $*" | tee -a "${LOG_FILE}"
}

log_success() {
    echo -e "${SUCCESS_COLOR}[PASS]${NC} $*" | tee -a "${LOG_FILE}"
    ((PASSED_CHECKS++))
}

log_error() {
    echo -e "${ERROR_COLOR}[FAIL]${NC} $*" | tee -a "${LOG_FILE}"
    ((FAILED_CHECKS++))
}

log_warning() {
    echo -e "${WARNING_COLOR}[WARN]${NC} $*" | tee -a "${LOG_FILE}"
    ((WARNINGS++))
}

# Check execution wrapper
check() {
    local check_name="$1"
    shift
    ((TOTAL_CHECKS++))
    
    log_info "Checking: ${check_name}"
    
    if "$@"; then
        log_success "${check_name}"
        return 0
    else
        log_error "${check_name}"
        return 1
    fi
}

# Individual check functions
check_directory_structure() {
    local required_dirs=(
        "${ADDON_DIR}/src"
        "${ADDON_DIR}/linux/src" 
        "${ADDON_DIR}/linux/include"
        "${ADDON_DIR}/webserver"
        "${ADDON_DIR}/rootfs/etc/s6-overlay/s6-rc.d"
        "${ADDON_DIR}/translations"
    )
    
    for dir in "${required_dirs[@]}"; do
        [[ -d "${dir}" ]] || return 1
    done
    return 0
}

check_required_files() {
    local required_files=(
        "${ADDON_DIR}/config.yaml"
        "${ADDON_DIR}/Dockerfile" 
        "${ADDON_DIR}/build.json"
        "${ADDON_DIR}/startup.sh"
        "${ADDON_DIR}/src/vbusdecoder.cpp"
        "${ADDON_DIR}/src/vbusdecoder.h"
        "${ADDON_DIR}/linux/src/LinuxSerial.cpp"
        "${ADDON_DIR}/linux/include/LinuxSerial.h"
        "${ADDON_DIR}/webserver/main.cpp"
    )
    
    for file in "${required_files[@]}"; do
        [[ -f "${file}" ]] || return 1
    done
    return 0
}

check_executable_permissions() {
    local executables=(
        "${ADDON_DIR}/startup.sh"
        "${ADDON_DIR}/rootfs/etc/s6-overlay/s6-rc.d/viessmann-decoder/run"
    )
    
    for executable in "${executables[@]}"; do
        [[ -x "${executable}" ]] || return 1
    done
    return 0
}

check_yaml_syntax() {
    if command -v yq &>/dev/null; then
        yq eval '.' "${ADDON_DIR}/config.yaml" >/dev/null 2>&1
    elif command -v python3 &>/dev/null; then
        python3 -c "
import yaml
with open('${ADDON_DIR}/config.yaml') as f:
    yaml.safe_load(f)
" 2>/dev/null
    else
        log_warning "No YAML validator available, skipping syntax check"
        return 0
    fi
}

check_json_syntax() {
    if command -v jq &>/dev/null; then
        jq empty "${ADDON_DIR}/build.json" >/dev/null 2>&1
    elif command -v python3 &>/dev/null; then
        python3 -c "
import json
with open('${ADDON_DIR}/build.json') as f:
    json.load(f)
" 2>/dev/null
    else
        log_warning "No JSON validator available, skipping syntax check"
        return 0
    fi
}

check_config_schema() {
    local config="${ADDON_DIR}/config.yaml"
    
    # Check required top-level fields
    grep -q "^name:" "${config}" || return 1
    grep -q "^version:" "${config}" || return 1
    grep -q "^slug:" "${config}" || return 1
    grep -q "^description:" "${config}" || return 1
    grep -q "^arch:" "${config}" || return 1
    grep -q "^init:" "${config}" || return 1
    
    # Check options schema
    grep -q "serial_port:" "${config}" || return 1
    grep -q "baud_rate:" "${config}" || return 1
    grep -q "protocol:" "${config}" || return 1
    grep -q "serial_config:" "${config}" || return 1
    
    # Check for duplicate fields that cause schema errors
    local log_level_count
    log_level_count=$(grep -c "log_level:" "${config}" 2>/dev/null || echo "0")
    [[ ${log_level_count} -le 1 ]] || return 1
    
    return 0
}

check_dockerfile_syntax() {
    local dockerfile="${ADDON_DIR}/Dockerfile"
    
    # Basic Dockerfile validation
    grep -q "^FROM " "${dockerfile}" || return 1
    grep -q "WORKDIR" "${dockerfile}" || return 1
    grep -q "COPY" "${dockerfile}" || return 1
    grep -q "RUN" "${dockerfile}" || return 1
    grep -q "HEALTHCHECK" "${dockerfile}" || return 1
    
    return 0
}

check_cpp_basic_syntax() {
    if ! command -v g++ &>/dev/null; then
        log_warning "g++ not available, skipping C++ syntax check"
        return 0
    fi
    
    # Test compilation of main source files
    local temp_dir
    temp_dir=$(mktemp -d)
    
    # Basic syntax check for main files
    g++ -fsyntax-only \
        -I"${ADDON_DIR}/linux/include" \
        -I"${ADDON_DIR}/src" \
        -std=c++17 \
        "${ADDON_DIR}/src/vbusdecoder.cpp" 2>/dev/null || {
        rm -rf "${temp_dir}"
        return 1
    }
    
    g++ -fsyntax-only \
        -I"${ADDON_DIR}/linux/include" \
        -std=c++17 \
        "${ADDON_DIR}/linux/src/LinuxSerial.cpp" 2>/dev/null || {
        rm -rf "${temp_dir}"
        return 1
    }
    
    rm -rf "${temp_dir}"
    return 0
}

check_service_configuration() {
    # Check s6-overlay v3 service configuration
    [[ -f "${ADDON_DIR}/rootfs/etc/s6-overlay/s6-rc.d/viessmann-decoder/type" ]] || return 1
    [[ -f "${ADDON_DIR}/rootfs/etc/s6-overlay/s6-rc.d/viessmann-decoder/run" ]] || return 1
    [[ -f "${ADDON_DIR}/rootfs/etc/s6-overlay/s6-rc.d/user/contents.d/viessmann-decoder" ]] || return 1
    
    # Check service type is correctly set
    grep -q "bundle" "${ADDON_DIR}/rootfs/etc/s6-overlay/s6-rc.d/viessmann-decoder/type" || return 1
    
    return 0
}

check_webserver_endpoints() {
    local webserver_cpp="${ADDON_DIR}/webserver/main.cpp"
    
    # Check for essential HTTP endpoints
    grep -q '"/health"' "${webserver_cpp}" || return 1
    grep -q '"/"' "${webserver_cpp}" || return 1
    
    # Check for signal handling
    grep -q "signalHandler" "${webserver_cpp}" || return 1
    
    # Check for microhttpd usage
    grep -q "#include.*microhttpd" "${webserver_cpp}" || return 1
    
    return 0
}

check_build_configuration() {
    local build_json="${ADDON_DIR}/build.json"
    
    # Check required architectures
    grep -q "amd64" "${build_json}" || return 1
    grep -q "aarch64" "${build_json}" || return 1
    grep -q "armv7" "${build_json}" || return 1
    
    # Check base image references
    grep -q "ghcr.io/home-assistant" "${build_json}" || return 1
    
    return 0
}

check_translations() {
    if [[ -d "${ADDON_DIR}/translations" ]]; then
        # Check for English translation (required)
        [[ -f "${ADDON_DIR}/translations/en.yaml" ]] || return 1
        
        # Validate English translation syntax
        if command -v yq &>/dev/null; then
            yq eval '.' "${ADDON_DIR}/translations/en.yaml" >/dev/null 2>&1 || return 1
        fi
    fi
    
    return 0
}

check_startup_script() {
    local startup="${ADDON_DIR}/startup.sh"
    
    # Check for bashio usage
    grep -q "bashio::" "${startup}" || return 1
    
    # Check for configuration validation
    grep -q "validate_config" "${startup}" || return 1
    
    # Check for error handling
    grep -q "set -e" "${startup}" || return 1
    
    return 0
}

check_security_permissions() {
    local config="${ADDON_DIR}/config.yaml"
    
    # Check for required security permissions
    if grep -q "uart:" "${config}"; then
        grep -q "uart: true" "${config}" || return 1
    fi
    
    if grep -q "privileged:" "${config}"; then
        grep -q "SYS_RAWIO" "${config}" || return 1
    fi
    
    return 0
}

# Docker-specific checks (if Docker is available)
check_docker_build() {
    if ! command -v docker &>/dev/null; then
        log_warning "Docker not available, skipping build test"
        return 0
    fi
    
    log_info "Testing Docker build (this may take a few minutes)..."
    
    local test_tag="viessmann-validation-$$"
    if docker build -t "${test_tag}" "${ADDON_DIR}" >/dev/null 2>&1; then
        # Clean up test image
        docker rmi "${test_tag}" >/dev/null 2>&1 || true
        return 0
    else
        return 1
    fi
}

# Main validation function
main() {
    log_info "=================================================="
    log_info "Viessmann Decoder Add-on Comprehensive Validation"
    log_info "=================================================="
    log_info "Starting validation at $(date)"
    
    # Clear previous log
    > "${LOG_FILE}"
    
    # Change to addon directory
    cd "${ADDON_DIR}"
    
    # File and directory structure checks
    log_info ""
    log_info "=== File Structure Validation ==="
    check "Directory structure" check_directory_structure
    check "Required files present" check_required_files
    check "Executable permissions" check_executable_permissions
    
    # Configuration file validation
    log_info ""
    log_info "=== Configuration Validation ==="
    check "YAML syntax (config.yaml)" check_yaml_syntax
    check "JSON syntax (build.json)" check_json_syntax
    check "Config schema compliance" check_config_schema
    check "Build configuration" check_build_configuration
    check "Security permissions" check_security_permissions
    
    # Source code validation
    log_info ""
    log_info "=== Source Code Validation ==="
    check "Dockerfile syntax" check_dockerfile_syntax
    check "C++ basic syntax" check_cpp_basic_syntax
    check "Webserver endpoints" check_webserver_endpoints
    
    # Service configuration validation
    log_info ""
    log_info "=== Service Configuration ==="
    check "s6-overlay service config" check_service_configuration
    check "Startup script validation" check_startup_script
    
    # Optional checks
    log_info ""
    log_info "=== Optional Components ==="
    check "Translation files" check_translations
    
    # Build validation (if Docker available)
    log_info ""
    log_info "=== Build Validation ==="
    check "Docker build test" check_docker_build
    
    # Summary
    log_info ""
    log_info "============================================"
    log_info "VALIDATION SUMMARY"
    log_info "============================================"
    log_info "Total Checks: ${TOTAL_CHECKS}"
    
    if [[ ${PASSED_CHECKS} -eq ${TOTAL_CHECKS} ]]; then
        log_success "All checks passed! (${PASSED_CHECKS}/${TOTAL_CHECKS})"
        log_info "Add-on is ready for deployment"
        if [[ ${WARNINGS} -gt 0 ]]; then
            log_warning "Note: ${WARNINGS} warnings were issued"
        fi
        exit 0
    else
        log_error "Some checks failed! (${PASSED_CHECKS}/${TOTAL_CHECKS} passed, ${FAILED_CHECKS} failed)"
        if [[ ${WARNINGS} -gt 0 ]]; then
            log_warning "Additional warnings: ${WARNINGS}"
        fi
        log_error "Add-on requires fixes before deployment"
        log_error "Check ${LOG_FILE} for detailed results"
        exit 1
    fi
}

# Run main function
main "$@"