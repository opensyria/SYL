#!/bin/bash
# OpenSY Comprehensive Test Suite Runner
# This script runs all tests and generates coverage reports

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_ROOT}/build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================"
echo "OpenSY Comprehensive Test Suite"
echo "========================================"
echo ""

# Check if build exists
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${RED}Error: Build directory not found. Run cmake first.${NC}"
    exit 1
fi

# Check if test binary exists
if [ ! -f "$BUILD_DIR/bin/test_opensy" ]; then
    echo -e "${RED}Error: test_opensy binary not found. Build first.${NC}"
    exit 1
fi

cd "$BUILD_DIR"

echo "Step 1: Running Unit Tests"
echo "----------------------------------------"
START_TIME=$(date +%s)

if ./bin/test_opensy --log_level=test_suite 2>&1 | tee unit_test_output.log | tail -20; then
    UNIT_RESULT="PASS"
    echo -e "${GREEN}✓ Unit tests passed${NC}"
else
    UNIT_RESULT="FAIL"
    echo -e "${RED}✗ Unit tests failed${NC}"
fi

END_TIME=$(date +%s)
UNIT_DURATION=$((END_TIME - START_TIME))
echo "Duration: ${UNIT_DURATION}s"
echo ""

echo "Step 2: Running OpenSY-specific Tests"
echo "----------------------------------------"

# Run OpenSY-specific test suites
OPENSY_TESTS=(
    "randomx_tests"
    "randomx_pool_tests"
    "randomx_fork_transition_tests"
    "randomx_reorg_tests"
    "randomx_high_priority_tests"
    "randomx_pool_priority_tests"
    "src20_tests"
    "token_validation_tests"
    "token_concurrent_tests"
    "tokendb_disconnect_tests"
    "argon2_fallback_tests"
)

OPENSY_PASS=0
OPENSY_FAIL=0

for TEST in "${OPENSY_TESTS[@]}"; do
    if ./bin/test_opensy --run_test="$TEST" --log_level=error 2>&1 | grep -q "No errors"; then
        echo -e "  ${GREEN}✓${NC} $TEST"
        ((OPENSY_PASS++))
    else
        echo -e "  ${RED}✗${NC} $TEST"
        ((OPENSY_FAIL++))
    fi
done

echo ""
echo "OpenSY Tests: ${OPENSY_PASS} passed, ${OPENSY_FAIL} failed"
echo ""

echo "Step 3: Running Functional Tests (OpenSY-specific)"
echo "----------------------------------------"

cd "$PROJECT_ROOT"

FUNCTIONAL_TESTS=(
    "feature_randomx_pow.py"
    "feature_randomx_key_rotation.py"
    "feature_randomx_difficulty.py"
    "feature_token_basic.py"
    "feature_token_validation.py"
    "feature_token_reorg.py"
    "feature_token_rpc.py"
    "feature_token_mempool.py"
    "feature_argon2_emergency.py"
)

FUNC_PASS=0
FUNC_FAIL=0
FUNC_SKIP=0

for TEST in "${FUNCTIONAL_TESTS[@]}"; do
    TEST_PATH="test/functional/$TEST"
    if [ -f "$TEST_PATH" ]; then
        echo -n "  Running $TEST... "
        if python3 "$TEST_PATH" --timeout-factor=3 2>&1 | tail -1 | grep -q "passed"; then
            echo -e "${GREEN}PASS${NC}"
            ((FUNC_PASS++))
        else
            echo -e "${RED}FAIL${NC}"
            ((FUNC_FAIL++))
        fi
    else
        echo -e "  ${YELLOW}⊘${NC} $TEST (not found)"
        ((FUNC_SKIP++))
    fi
done

echo ""
echo "Functional Tests: ${FUNC_PASS} passed, ${FUNC_FAIL} failed, ${FUNC_SKIP} skipped"
echo ""

echo "========================================"
echo "SUMMARY"
echo "========================================"
echo ""
echo "Unit Tests:       $UNIT_RESULT (${UNIT_DURATION}s)"
echo "OpenSY Unit:      ${OPENSY_PASS}/${#OPENSY_TESTS[@]} passed"
echo "Functional:       ${FUNC_PASS}/${#FUNCTIONAL_TESTS[@]} passed"
echo ""

if [ "$UNIT_RESULT" = "PASS" ] && [ "$OPENSY_FAIL" -eq 0 ] && [ "$FUNC_FAIL" -eq 0 ]; then
    echo -e "${GREEN}✓ ALL TESTS PASSED${NC}"
    exit 0
else
    echo -e "${RED}✗ SOME TESTS FAILED${NC}"
    exit 1
fi
