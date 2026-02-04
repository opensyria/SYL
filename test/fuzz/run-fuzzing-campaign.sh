#!/bin/bash
# OpenSY Continuous Fuzzing Campaign
# Run for 7+ days to find edge cases in consensus-critical code

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OPENSY_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
FUZZ_BUILD_DIR="$OPENSY_ROOT/build-fuzz"
CORPUS_DIR="$OPENSY_ROOT/fuzz-corpus"
CRASH_DIR="$OPENSY_ROOT/fuzz-crashes"
LOG_DIR="$OPENSY_ROOT/fuzz-logs"
REPORT_FILE="$LOG_DIR/fuzzing-report.md"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Default settings
JOBS=${FUZZ_JOBS:-4}
DURATION=${FUZZ_DURATION:-604800}  # 7 days in seconds
MAX_LEN=${FUZZ_MAX_LEN:-10000}

# Priority targets for OpenSY
FUZZ_TARGETS=(
    "fuzz_script_interpreter"     # Consensus-critical: script execution
    "fuzz_block_deserialize"      # DoS vector: block parsing
    "fuzz_transaction"            # Transaction validation
    "fuzz_header_deserialize"     # Block header parsing
    "fuzz_randomx_header"         # OpenSY-specific: RandomX validation
    "fuzz_script"                 # General script parsing
    "fuzz_addrman"                # Address manager
    "fuzz_netaddress"             # Network address parsing
)

echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}  OpenSY Continuous Fuzzing Campaign${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo
echo "Configuration:"
echo "  Jobs:     $JOBS parallel fuzzing jobs"
echo "  Duration: $((DURATION / 3600)) hours ($((DURATION / 86400)) days)"
echo "  Max len:  $MAX_LEN bytes per input"
echo

# Check prerequisites
check_prerequisites() {
    echo -e "${YELLOW}Checking prerequisites...${NC}"
    
    if ! command -v clang++ &> /dev/null; then
        echo -e "${RED}Error: clang++ not found${NC}"
        echo "Install with: brew install llvm (macOS) or apt install clang (Linux)"
        exit 1
    fi
    echo -e "  ✓ clang++: $(clang++ --version | head -1)"
    
    if ! command -v llvm-symbolizer &> /dev/null && ! command -v /usr/local/opt/llvm/bin/llvm-symbolizer &> /dev/null; then
        echo -e "${YELLOW}  ⚠ llvm-symbolizer not in PATH (stack traces may be less readable)${NC}"
    else
        echo -e "  ✓ llvm-symbolizer available"
    fi
    
    echo
}

# Build fuzz targets
build_fuzz_targets() {
    echo -e "${YELLOW}Building fuzz targets with sanitizers...${NC}"
    
    cd "$OPENSY_ROOT"
    
    # Create build directory
    mkdir -p "$FUZZ_BUILD_DIR"
    
    # Configure with fuzzing enabled
    cmake -B "$FUZZ_BUILD_DIR" \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DSANITIZERS=fuzzer,address,undefined \
        -DCMAKE_BUILD_TYPE=Debug \
        -DENABLE_WALLET=OFF \
        -DBUILD_TESTS=OFF \
        -DBUILD_BENCH=OFF \
        2>&1 | tail -5
    
    # Build
    cmake --build "$FUZZ_BUILD_DIR" --target fuzz -- -j$JOBS 2>&1 | tail -10
    
    echo -e "${GREEN}  ✓ Fuzz targets built${NC}"
    echo
}

# Initialize corpus
init_corpus() {
    echo -e "${YELLOW}Initializing corpus directories...${NC}"
    
    mkdir -p "$CORPUS_DIR"
    mkdir -p "$CRASH_DIR"
    mkdir -p "$LOG_DIR"
    
    # Create subdirs for each target
    for target in "${FUZZ_TARGETS[@]}"; do
        mkdir -p "$CORPUS_DIR/$target"
        mkdir -p "$CRASH_DIR/$target"
    done
    
    # Seed with existing test vectors if available
    if [ -d "$OPENSY_ROOT/test/fuzz/corpora" ]; then
        echo "  Seeding from existing corpora..."
        cp -r "$OPENSY_ROOT/test/fuzz/corpora/"* "$CORPUS_DIR/" 2>/dev/null || true
    fi
    
    echo -e "${GREEN}  ✓ Corpus initialized${NC}"
    echo
}

# Run fuzzing for a single target
fuzz_target() {
    local target=$1
    local duration=$2
    local log_file="$LOG_DIR/${target}.log"
    
    local fuzz_binary="$FUZZ_BUILD_DIR/src/test/fuzz/$target"
    if [ ! -f "$fuzz_binary" ]; then
        fuzz_binary="$FUZZ_BUILD_DIR/bin/$target"
    fi
    
    if [ ! -f "$fuzz_binary" ]; then
        echo -e "${YELLOW}  ⚠ Skipping $target (binary not found)${NC}"
        return
    fi
    
    echo -e "${BLUE}Fuzzing: $target${NC}"
    echo "  Binary: $fuzz_binary"
    echo "  Corpus: $CORPUS_DIR/$target"
    echo "  Log:    $log_file"
    
    # Set up environment for sanitizers
    export ASAN_OPTIONS="detect_leaks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1:halt_on_error=0"
    export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0:report_error_type=1"
    
    # Run fuzzer
    timeout ${duration}s "$fuzz_binary" \
        "$CORPUS_DIR/$target" \
        -artifact_prefix="$CRASH_DIR/$target/" \
        -max_len=$MAX_LEN \
        -jobs=$JOBS \
        -workers=$JOBS \
        -print_final_stats=1 \
        2>&1 | tee "$log_file" &
    
    FUZZ_PID=$!
    echo "  PID: $FUZZ_PID"
    echo
}

# Run all targets in parallel (each with a subset of cores)
run_parallel_campaign() {
    local duration_per_target=$((DURATION / ${#FUZZ_TARGETS[@]}))
    
    echo -e "${YELLOW}Starting parallel fuzzing campaign...${NC}"
    echo "Duration per target: $((duration_per_target / 3600)) hours"
    echo
    
    PIDS=()
    
    for target in "${FUZZ_TARGETS[@]}"; do
        fuzz_target "$target" "$duration_per_target"
        PIDS+=($FUZZ_PID)
        sleep 2  # Stagger starts
    done
    
    echo -e "${GREEN}All fuzzers started!${NC}"
    echo "PIDs: ${PIDS[*]}"
    echo
    echo "To monitor:"
    echo "  watch -n 60 'find $CRASH_DIR -name \"crash-*\" | wc -l'"
    echo "  tail -f $LOG_DIR/*.log"
    echo
    echo "To stop:"
    echo "  kill ${PIDS[*]}"
    echo
}

# Run targets sequentially (more thorough, one at a time)
run_sequential_campaign() {
    local duration_per_target=$((DURATION / ${#FUZZ_TARGETS[@]}))
    
    echo -e "${YELLOW}Starting sequential fuzzing campaign...${NC}"
    echo "Duration per target: $((duration_per_target / 3600)) hours"
    echo
    
    for target in "${FUZZ_TARGETS[@]}"; do
        fuzz_target "$target" "$duration_per_target"
        wait $FUZZ_PID 2>/dev/null || true
    done
}

# Generate report
generate_report() {
    echo -e "${YELLOW}Generating fuzzing report...${NC}"
    
    cat > "$REPORT_FILE" << EOF
# OpenSY Fuzzing Campaign Report

**Generated:** $(date)
**Duration:** $((DURATION / 86400)) days
**Targets:** ${#FUZZ_TARGETS[@]}

## Crash Summary

| Target | Crashes | Unique | Status |
|--------|---------|--------|--------|
EOF
    
    for target in "${FUZZ_TARGETS[@]}"; do
        crash_count=$(find "$CRASH_DIR/$target" -name "crash-*" 2>/dev/null | wc -l)
        unique_count=$(find "$CRASH_DIR/$target" -name "crash-*" 2>/dev/null | xargs -I{} md5sum {} 2>/dev/null | awk '{print $1}' | sort -u | wc -l)
        
        if [ "$crash_count" -gt 0 ]; then
            status="⚠️ REVIEW"
        else
            status="✅ Clean"
        fi
        
        echo "| $target | $crash_count | $unique_count | $status |" >> "$REPORT_FILE"
    done
    
    cat >> "$REPORT_FILE" << EOF

## Corpus Growth

| Target | Initial | Final | Growth |
|--------|---------|-------|--------|
EOF
    
    for target in "${FUZZ_TARGETS[@]}"; do
        count=$(find "$CORPUS_DIR/$target" -type f 2>/dev/null | wc -l)
        echo "| $target | - | $count | - |" >> "$REPORT_FILE"
    done
    
    cat >> "$REPORT_FILE" << EOF

## Next Steps

1. Review all crash files in \`$CRASH_DIR\`
2. Minimize reproducers: \`./minimize_crash.sh <crash_file>\`
3. Create bug reports for confirmed issues
4. Extend corpus and re-run if clean

## Commands

\`\`\`bash
# Reproduce a crash
./$FUZZ_BUILD_DIR/bin/fuzz_script_interpreter $CRASH_DIR/fuzz_script_interpreter/crash-xxx

# Minimize crash
./$FUZZ_BUILD_DIR/bin/fuzz_script_interpreter -minimize_crash=1 -exact_artifact_path=minimized.bin crash-xxx

# Merge corpora
./$FUZZ_BUILD_DIR/bin/fuzz_script_interpreter -merge=1 $CORPUS_DIR/fuzz_script_interpreter new_inputs/
\`\`\`
EOF
    
    echo -e "${GREEN}  ✓ Report generated: $REPORT_FILE${NC}"
}

# Status check
check_status() {
    echo -e "${BLUE}Fuzzing Status${NC}"
    echo
    
    echo "Active fuzzer processes:"
    pgrep -f "fuzz_" | while read pid; do
        ps -p $pid -o pid,etime,command 2>/dev/null | tail -1
    done
    echo
    
    echo "Crashes found:"
    for target in "${FUZZ_TARGETS[@]}"; do
        count=$(find "$CRASH_DIR/$target" -name "crash-*" 2>/dev/null | wc -l)
        if [ "$count" -gt 0 ]; then
            echo -e "  ${RED}$target: $count crashes${NC}"
        else
            echo -e "  ${GREEN}$target: clean${NC}"
        fi
    done
    echo
    
    echo "Corpus sizes:"
    for target in "${FUZZ_TARGETS[@]}"; do
        count=$(find "$CORPUS_DIR/$target" -type f 2>/dev/null | wc -l)
        echo "  $target: $count inputs"
    done
}

# Main
case "${1:-start}" in
    start|run)
        check_prerequisites
        build_fuzz_targets
        init_corpus
        run_parallel_campaign
        ;;
    sequential)
        check_prerequisites
        build_fuzz_targets
        init_corpus
        run_sequential_campaign
        generate_report
        ;;
    build)
        check_prerequisites
        build_fuzz_targets
        ;;
    status)
        check_status
        ;;
    report)
        generate_report
        cat "$REPORT_FILE"
        ;;
    stop)
        echo "Stopping all fuzzers..."
        pkill -f "fuzz_" || echo "No fuzzers running"
        ;;
    *)
        echo "Usage: $0 {start|sequential|build|status|report|stop}"
        echo
        echo "Commands:"
        echo "  start      - Build and start parallel fuzzing (background)"
        echo "  sequential - Run targets one at a time (foreground)"
        echo "  build      - Build fuzz targets only"
        echo "  status     - Show current fuzzing status"
        echo "  report     - Generate summary report"
        echo "  stop       - Stop all running fuzzers"
        echo
        echo "Environment variables:"
        echo "  FUZZ_JOBS=4        Number of parallel jobs"
        echo "  FUZZ_DURATION=604800  Total duration in seconds (default: 7 days)"
        echo "  FUZZ_MAX_LEN=10000 Maximum input length"
        exit 1
        ;;
esac
