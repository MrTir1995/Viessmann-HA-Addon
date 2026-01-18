#!/bin/bash
# Comprehensive test suite for Viessmann Decoder Add-on
# Tests configuration, build, runtime, and integration aspects

set -euo pipefail

# Constants
readonly ADDON_DIR="/workspaces/developers.home-assistant/Viessmann-Home-Assistant-Addon-/viessmann-decoder"
readonly TEST_LOG="${ADDON_DIR}/test_results.log"
readonly TEMP_DIR="${ADDON_DIR}/test_temp"

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_SKIPPED=0

# Colors for output
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m' # No Color

# Logging functions
log_test() {
    echo -e "[TEST] ${BLUE}$*${NC}" | tee -a "${TEST_LOG}"
}

log_pass() {
    echo -e "[PASS] ${GREEN}$*${NC}" | tee -a "${TEST_LOG}"
    ((TESTS_PASSED++))
}

log_fail() {
    echo -e "[FAIL] ${RED}$*${NC}" | tee -a "${TEST_LOG}"
    ((TESTS_FAILED++))
}

log_skip() {
    echo -e "[SKIP] ${YELLOW}$*${NC}" | tee -a "${TEST_LOG}"
    ((TESTS_SKIPPED++))
}

log_info() {
    echo -e "[INFO] $*" | tee -a "${TEST_LOG}"
}

# Test execution wrapper
run_test() {
    local test_name="$1"
    local test_function="$2"
    
    ((TESTS_RUN++))
    log_test "Running: ${test_name}"
    
    if "${test_function}"; then
        log_pass "${test_name}"
        return 0
    else
        log_fail "${test_name}"
        return 1
    fi
}

# Cleanup function
cleanup() {
    local exit_code=$?
    
    if [[ -d "${TEMP_DIR}" ]]; then
        rm -rf "${TEMP_DIR}"
    fi
    
    # Clean up any test containers
    docker ps -a --filter "name=viessmann-test-*" -q | xargs -r docker rm -f &>/dev/null || true
    
    # Summary
    echo -e "\n=== TEST SUMMARY ===" | tee -a "${TEST_LOG}"
    echo -e "Tests Run: ${TESTS_RUN}" | tee -a "${TEST_LOG}"
    echo -e "${GREEN}Passed: ${TESTS_PASSED}${NC}" | tee -a "${TEST_LOG}"
    echo -e "${RED}Failed: ${TESTS_FAILED}${NC}" | tee -a "${TEST_LOG}"
    echo -e "${YELLOW}Skipped: ${TESTS_SKIPPED}${NC}" | tee -a "${TEST_LOG}"
    
    if [[ ${TESTS_FAILED} -gt 0 ]]; then
        echo -e "${RED}Some tests failed. Check ${TEST_LOG} for details.${NC}"
        exit_code=1
    else
        echo -e "${GREEN}All tests passed!${NC}"
    fi
    
    exit ${exit_code}
}

trap cleanup EXIT

# Test functions
test_config_yaml_syntax() {
    if command -v yq &> /dev/null; then
        yq eval '.' "${ADDON_DIR}/config.yaml" > /dev/null
    elif command -v python3 &> /dev/null; then
        python3 -c "import yaml; yaml.safe_load(open('${ADDON_DIR}/config.yaml'))"
    else
        log_skip "test_config_yaml_syntax - no YAML parser available"
        return 0
    fi
}

test_config_yaml_required_fields() {
    local config="${ADDON_DIR}/config.yaml"
    
    # Check for required fields
    if ! grep -q "^name:" "${config}"; then
        return 1
    fi
    
    if ! grep -q "^version:" "${config}"; then
        return 1
    fi
    
    if ! grep -q "^slug:" "${config}"; then
        return 1
    fi
    
    if ! grep -q "^description:" "${config}"; then
        return 1
    fi
    
    return 0
}

test_dockerfile_syntax() {
    # Basic Dockerfile syntax check
    if ! docker build --dry-run "${ADDON_DIR}" &>/dev/null; then
        # Try a more basic check
        if ! grep -q "^FROM " "${ADDON_DIR}/Dockerfile"; then
            return 1
        fi
    fi
    return 0
}

test_source_files_exist() {
    local required_files=(
        "${ADDON_DIR}/src/vbusdecoder.cpp"
        "${ADDON_DIR}/src/vbusdecoder.h"
        "${ADDON_DIR}/linux/src/LinuxSerial.cpp"
        "${ADDON_DIR}/linux/src/Arduino.cpp"
        "${ADDON_DIR}/linux/include/LinuxSerial.h"
        "${ADDON_DIR}/webserver/main.cpp"
    )
    
    for file in "${required_files[@]}"; do
        if [[ ! -f "${file}" ]]; then
            log_info "Missing required file: ${file}"
            return 1
        fi
    done
    
    return 0
}

test_cpp_syntax_basic() {
    # Basic C++ syntax check using compiler (if available)
    if command -v g++ &> /dev/null; then
        mkdir -p "${TEMP_DIR}"
        
        # Test main source files for basic syntax
        if ! g++ -fsyntax-only \
            -I"${ADDON_DIR}/linux/include" \
            -I"${ADDON_DIR}/src" \
            "${ADDON_DIR}/src/vbusdecoder.cpp" \
            2>/dev/null; then
            return 1
        fi
        
        if ! g++ -fsyntax-only \
            -I"${ADDON_DIR}/linux/include" \
            "${ADDON_DIR}/linux/src/LinuxSerial.cpp" \
            2>/dev/null; then
            return 1
        fi
    else
        log_skip "test_cpp_syntax_basic - g++ not available"
        return 0
    fi
    
    return 0
}

test_service_scripts_exist() {
    local required_scripts=(
        "${ADDON_DIR}/startup.sh"
        "${ADDON_DIR}/rootfs/etc/s6-overlay/s6-rc.d/viessmann-decoder/run"
        "${ADDON_DIR}/rootfs/etc/s6-overlay/s6-rc.d/viessmann-decoder/type"
    )
    
    for script in "${required_scripts[@]}"; do
        if [[ ! -f "${script}" ]]; then
            log_info "Missing service script: ${script}"
            return 1
        fi
    done
    
    return 0
}

test_service_scripts_executable() {
    local executable_scripts=(
        "${ADDON_DIR}/startup.sh"
        "${ADDON_DIR}/rootfs/etc/s6-overlay/s6-rc.d/viessmann-decoder/run"
    )
    
    for script in "${executable_scripts[@]}"; do
        if [[ -f "${script}" ]] && [[ ! -x "${script}" ]]; then
            log_info "Script not executable: ${script}"
            return 1
        fi
    done
    
    return 0
}

test_build_json_syntax() {
    if command -v jq &> /dev/null; then
        jq empty "${ADDON_DIR}/build.json" &>/dev/null
    elif command -v python3 &> /dev/null; then
        python3 -c "import json; json.load(open('${ADDON_DIR}/build.json'))" &>/dev/null
    else
        log_skip "test_build_json_syntax - no JSON parser available"
        return 0
    fi
}

test_translations_exist() {
    if [[ -d "${ADDON_DIR}/translations" ]]; then
        if [[ ! -f "${ADDON_DIR}/translations/en.yaml" ]]; then
            log_info "English translation missing"
            return 1
        fi
    fi
    return 0
}

test_dockerfile_health_check() {
    if ! grep -q "HEALTHCHECK" "${ADDON_DIR}/Dockerfile"; then
        log_info "No HEALTHCHECK instruction found in Dockerfile"
        return 1
    fi
    
    return 0
}

test_docker_image_build() {
    if command -v docker &> /dev/null; then
        local test_tag="viessmann-test-$$"
        
        if docker build -t "${test_tag}" "${ADDON_DIR}" &>/dev/null; then
            # Clean up the test image
            docker rmi "${test_tag}" &>/dev/null || true
            return 0
        else
            return 1
        fi
    else
        log_skip "test_docker_image_build - Docker not available"
        return 0
    fi
}

test_config_schema_compliance() {
    local config="${ADDON_DIR}/config.yaml"
    
    # Check that required fields are present and not duplicated
    local log_count
    log_count=$(grep -c "^[[:space:]]*log_level:" "${config}" 2>/dev/null || echo "0")
    
    if [[ ${log_count} -gt 1 ]]; then
        log_info "Duplicate log_level fields found"
        return 1
    fi
    
    # Check options schema
    if ! grep -q "serial_port:" "${config}"; then
        log_info "serial_port option missing from config"
        return 1
    fi
    
    return 0
}

test_webserver_endpoints() {
    local main_cpp="${ADDON_DIR}/webserver/main.cpp"
    
    # Check for essential endpoints
    if ! grep -q '"/health"' "${main_cpp}"; then
        log_info "Health endpoint missing from webserver"
        return 1
    fi
    
    if ! grep -q '"/"' "${main_cpp}"; then
        log_info "Root endpoint missing from webserver"
        return 1
    fi
    
    return 0
}

# Main test execution
main() {
    log_info "=== Viessmann Decoder Addon Test Suite ==="
    log_info "Test started at $(date)"
    
    # Clear previous test log
    > "${TEST_LOG}"
    
    # Create temp directory
    mkdir -p "${TEMP_DIR}"
    
    # Change to addon directory
    cd "${ADDON_DIR}"
    
    # Configuration tests
    log_info "\n--- Configuration Tests ---"
    run_test "YAML syntax validation" test_config_yaml_syntax
    run_test "Required config fields" test_config_yaml_required_fields
    run_test "Config schema compliance" test_config_schema_compliance
    run_test "Build.json syntax" test_build_json_syntax
    run_test "Translation files" test_translations_exist
    
    # Source code tests
    log_info "\n--- Source Code Tests ---"
    run_test "Required source files exist" test_source_files_exist
    run_test "Basic C++ syntax check" test_cpp_syntax_basic
    run_test "Dockerfile syntax" test_dockerfile_syntax
    run_test "Docker health check" test_dockerfile_health_check
    run_test "Webserver endpoints" test_webserver_endpoints
    
    # Service tests
    log_info "\n--- Service Tests ---"
    run_test "Service scripts exist" test_service_scripts_exist
    run_test "Service scripts executable" test_service_scripts_executable
    
    # Build tests
    log_info "\n--- Build Tests ---"
    run_test "Docker image build" test_docker_image_build
    
    log_info "\n=== Test Suite Complete ==="
}

# Run main function
main "$@"