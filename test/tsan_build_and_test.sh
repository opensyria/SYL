#!/bin/bash
# OpenSY ThreadSanitizer (TSAN) Build and Test Script
# BLOCKER 2: Verify no data races in concurrent code paths
#
# This script:
# 1. Builds OpenSY with ThreadSanitizer enabled
# 2. Runs the full test suite under TSAN
# 3. Checks for data race warnings
# 4. Reports results
#
# Usage: ./test/tsan_build_and_test.sh
#
# Requirements:
# - Clang compiler with TSAN support
# - ~30 minutes for build + tests

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build_tsan"
RESULTS_FILE="$PROJECT_ROOT/test/tsan_results.md"

echo "=========================================="
echo "  OpenSY ThreadSanitizer Build & Test"
echo "=========================================="
echo ""
echo "Project root: $PROJECT_ROOT"
echo "Build dir:    $BUILD_DIR"
echo ""

# Check for compiler
if command -v clang++ &>/dev/null; then
    CXX_COMPILER="clang++"
    C_COMPILER="clang"
elif command -v g++ &>/dev/null; then
    CXX_COMPILER="g++"
    C_COMPILER="gcc"
else
    echo "❌ ERROR: No C++ compiler found"
    exit 1
fi

echo "Using compiler: $CXX_COMPILER"
echo ""

cd "$PROJECT_ROOT"

# Step 1: Configure with TSAN
echo "[1/4] Configuring build with ThreadSanitizer..."

# Note: TSAN and ASAN are mutually exclusive
cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER="$C_COMPILER" \
    -DCMAKE_CXX_COMPILER="$CXX_COMPILER" \
    -DCMAKE_C_FLAGS="-fsanitize=thread -g -O1" \
    -DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=thread" \
    -DBUILD_TESTS=ON \
    -DBUILD_DAEMON=ON \
    -DBUILD_CLI=ON \
    -DBUILD_GUI=OFF \
    2>&1 | tee "$BUILD_DIR/cmake_config.log"

echo "✓ Configuration complete"
echo ""

# Step 2: Build
echo "[2/4] Building with TSAN (this takes 10-20 minutes)..."
cmake --build "$BUILD_DIR" -j$(nproc) 2>&1 | tee "$BUILD_DIR/build.log"

if [ ! -f "$BUILD_DIR/bin/test_opensy" ]; then
    echo "❌ ERROR: Build failed - test_opensy not found"
    exit 1
fi

echo "✓ Build complete"
echo ""

# Step 3: Run tests
echo "[3/4] Running tests under ThreadSanitizer..."
echo "      (This may take 20-60 minutes)"
echo ""

# Set TSAN options for detailed output
export TSAN_OPTIONS="halt_on_error=0:second_deadlock_stack=1:history_size=4"

cd "$BUILD_DIR"

# Run tests and capture output
set +e
./bin/test_opensy --run_test=randomx* 2>&1 | tee tsan_randomx_output.log
RANDOMX_EXIT=$?

./bin/test_opensy --run_test=validation* 2>&1 | tee tsan_validation_output.log
VALIDATION_EXIT=$?

./bin/test_opensy 2>&1 | tee tsan_full_output.log
FULL_EXIT=$?
set -e

echo ""

# Step 4: Analyze results
echo "[4/4] Analyzing TSAN output..."
echo ""

# Count data race warnings
RACE_COUNT=$(grep -c "WARNING: ThreadSanitizer: data race" tsan_full_output.log 2>/dev/null || echo "0")
DEADLOCK_COUNT=$(grep -c "WARNING: ThreadSanitizer: lock-order-inversion" tsan_full_output.log 2>/dev/null || echo "0")

# Generate results file
cat > "$RESULTS_FILE" << EOF
# OpenSY ThreadSanitizer Test Results

## Test Configuration
- **Date:** $(date -u +"%Y-%m-%d %H:%M:%S UTC")
- **Platform:** $(uname -s) $(uname -m)
- **Compiler:** $CXX_COMPILER
- **Build Type:** Debug with -fsanitize=thread

## Results Summary

| Metric | Count | Status |
|--------|-------|--------|
| Data Races Detected | $RACE_COUNT | $([ "$RACE_COUNT" -eq 0 ] && echo "✅ PASS" || echo "❌ FAIL") |
| Deadlocks Detected | $DEADLOCK_COUNT | $([ "$DEADLOCK_COUNT" -eq 0 ] && echo "✅ PASS" || echo "❌ FAIL") |

## Test Suite Results

| Test Suite | Exit Code | Status |
|------------|-----------|--------|
| RandomX Tests | $RANDOMX_EXIT | $([ "$RANDOMX_EXIT" -eq 0 ] && echo "✅" || echo "⚠️") |
| Validation Tests | $VALIDATION_EXIT | $([ "$VALIDATION_EXIT" -eq 0 ] && echo "✅" || echo "⚠️") |
| Full Test Suite | $FULL_EXIT | $([ "$FULL_EXIT" -eq 0 ] && echo "✅" || echo "⚠️") |

## TSAN Warnings

EOF

if [ "$RACE_COUNT" -gt 0 ]; then
    echo "### Data Race Warnings" >> "$RESULTS_FILE"
    echo '```' >> "$RESULTS_FILE"
    grep -A 20 "WARNING: ThreadSanitizer: data race" tsan_full_output.log | head -100 >> "$RESULTS_FILE"
    echo '```' >> "$RESULTS_FILE"
fi

if [ "$DEADLOCK_COUNT" -gt 0 ]; then
    echo "### Deadlock Warnings" >> "$RESULTS_FILE"
    echo '```' >> "$RESULTS_FILE"
    grep -A 20 "WARNING: ThreadSanitizer: lock-order-inversion" tsan_full_output.log | head -100 >> "$RESULTS_FILE"
    echo '```' >> "$RESULTS_FILE"
fi

if [ "$RACE_COUNT" -eq 0 ] && [ "$DEADLOCK_COUNT" -eq 0 ]; then
    echo "No threading issues detected. ✅" >> "$RESULTS_FILE"
fi

cat >> "$RESULTS_FILE" << EOF

## Conclusion

$([ "$RACE_COUNT" -eq 0 ] && [ "$DEADLOCK_COUNT" -eq 0 ] && echo "**BLOCKER 2: RESOLVED ✅** - No threading issues detected." || echo "**BLOCKER 2: REQUIRES ATTENTION** - Threading issues found. See warnings above.")

## Full Logs

- TSAN Full Output: \`build_tsan/tsan_full_output.log\`
- RandomX Tests: \`build_tsan/tsan_randomx_output.log\`
- Validation Tests: \`build_tsan/tsan_validation_output.log\`
EOF

# Display summary
echo "=========================================="
echo "  TSAN Test Results"
echo "=========================================="
echo ""
echo "Data races detected:     $RACE_COUNT"
echo "Deadlocks detected:      $DEADLOCK_COUNT"
echo ""

if [ "$RACE_COUNT" -eq 0 ] && [ "$DEADLOCK_COUNT" -eq 0 ]; then
    echo "✅ BLOCKER 2: RESOLVED - No threading issues detected!"
else
    echo "❌ BLOCKER 2: REQUIRES ATTENTION"
    echo ""
    echo "Review the following logs:"
    echo "  - $BUILD_DIR/tsan_full_output.log"
    echo ""
    echo "Common fixes:"
    echo "  - Add mutex locks around shared data access"
    echo "  - Use atomic operations for simple counters"
    echo "  - Review lock ordering to prevent deadlocks"
fi

echo ""
echo "Full results saved to: $RESULTS_FILE"
echo ""
