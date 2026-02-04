# OpenSY Code Coverage Guide

## Overview

This guide explains how to generate code coverage reports for the OpenSY codebase using lcov/gcov.

## Prerequisites

Install lcov:

```bash
# macOS
brew install lcov

# Ubuntu/Debian
sudo apt-get install lcov

# Fedora/RHEL
sudo dnf install lcov
```

## Quick Start

### 1. Configure with Coverage Build Type

```bash
cmake -B build_coverage -DCMAKE_BUILD_TYPE=Coverage
cmake --build build_coverage -j$(nproc)
```

### 2. Generate Coverage Report

Run the integrated coverage script:

```bash
cd build_coverage
cmake --build . --target coverage
```

Or manually:

```bash
# Run tests
./bin/test_opensy

# Capture coverage data
lcov --capture --directory src --test-name test_opensy --output-file coverage.info

# Filter out system headers and dependencies
lcov --remove coverage.info \
    '/usr/*' \
    '*/leveldb/*' \
    '*/secp256k1/*' \
    '*/randomx/*' \
    '*/boost/*' \
    --output-file coverage_filtered.info

# Generate HTML report
genhtml coverage_filtered.info --output-directory coverage_report
```

### 3. View Report

Open `coverage_report/index.html` in a browser:

```bash
open coverage_report/index.html  # macOS
xdg-open coverage_report/index.html  # Linux
```

## Coverage Targets

### OpenSY-Specific Modules (Target: 80%+)

| Module | Description | Minimum Coverage |
|--------|-------------|------------------|
| `src/tokens/` | SRC-20 token system | 85% |
| `src/crypto/randomx*.cpp` | RandomX PoW integration | 80% |
| `src/pow.cpp` | Height-aware PoW validation | 80% |
| `src/wallet/tokens.cpp` | Wallet token support | 75% |
| `src/rpc/tokens.cpp` | Token RPC commands | 75% |

### Inherited Bitcoin Core (Monitor Only)

Coverage for inherited code is monitored but not required to meet targets:
- `src/validation.cpp`
- `src/net_processing.cpp`
- `src/consensus/`

## CI Integration

The GitHub Actions workflow includes coverage reporting:

```yaml
- name: Build with Coverage
  run: |
    cmake -B build -DCMAKE_BUILD_TYPE=Coverage
    cmake --build build -j$(nproc)

- name: Run Tests and Capture Coverage
  run: |
    cd build
    ./bin/test_opensy
    lcov --capture --directory src -o coverage.info
    lcov --remove coverage.info '/usr/*' '*/deps/*' -o filtered.info

- name: Upload Coverage
  uses: codecov/codecov-action@v4
  with:
    files: build/filtered.info
```

## Branch Coverage

For more detailed analysis, enable branch coverage:

```bash
lcov --capture --directory src --rc lcov_branch_coverage=1 -o coverage.info
genhtml coverage.info --branch-coverage -o coverage_report
```

## Interpreting Results

### Coverage Report Columns

- **Lines**: Percentage of executable lines covered
- **Functions**: Percentage of functions called at least once
- **Branches**: Percentage of branch conditions (if/switch) tested

### Color Coding

- 🟢 **Green (75%+)**: Good coverage
- 🟡 **Yellow (50-74%)**: Needs improvement
- 🔴 **Red (<50%)**: Critical - needs tests

## Troubleshooting

### "No data collected"

Ensure the build was configured with `-DCMAKE_BUILD_TYPE=Coverage`:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Coverage
cmake --build build --clean-first
```

### High coverage but missing critical paths

Use branch coverage to identify untested conditional paths:

```bash
lcov --rc lcov_branch_coverage=1 ...
```

### Coverage of specific files

Generate coverage for specific source files:

```bash
lcov --capture --directory src/tokens --output-file tokens_coverage.info
genhtml tokens_coverage.info --output-directory tokens_coverage
```

## See Also

- [TESTING_STRATEGY_AUDIT.md](../TESTING_STRATEGY_AUDIT.md) - Test gap analysis
- [CONTRIBUTING.md](../CONTRIBUTING.md) - Development guidelines
