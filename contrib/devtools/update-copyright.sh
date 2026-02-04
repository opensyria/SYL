#!/usr/bin/env bash
# Copyright Header Standardization Script for OpenSY
# 
# This script standardizes copyright headers across the codebase:
# - Files with original Bitcoin Core copyright get dual attribution
# - New OpenSY-specific files get OpenSY-only copyright
#
# Usage: ./contrib/devtools/update-copyright.sh [--dry-run]

set -e

# shellcheck disable=SC2034  # Variables are for future expansion and templates
DRY_RUN=false
if [[ "$1" == "--dry-run" ]]; then
    # shellcheck disable=SC2034
    DRY_RUN=true
    echo "DRY RUN MODE - No files will be modified"
fi

# Counters
MODIFIED=0
# shellcheck disable=SC2034
SKIPPED=0

# Standard copyright patterns
# shellcheck disable=SC2034
OPENSY_ONLY="// Copyright (c) 2025-present The OpenSY developers"
# shellcheck disable=SC2034
DUAL_COPYRIGHT_PATTERN="// Copyright (c) YEAR1-YEAR2 The Bitcoin Core developers
// Copyright (c) 2025-present The OpenSY developers"

update_file() {
    local file="$1"
    
    # Skip vendored/external code
    if [[ "$file" == *"/leveldb/"* ]] || \
       [[ "$file" == *"/secp256k1/"* ]] || \
       [[ "$file" == *"/univalue/"* ]] || \
       [[ "$file" == *"/minisketch/"* ]] || \
       [[ "$file" == *"/crc32c/"* ]]; then
        return
    fi
    
    # Check if file has Satoshi copyright (original Bitcoin files)
    if grep -q "Satoshi Nakamoto" "$file" 2>/dev/null; then
        # This is an original Bitcoin file - ensure dual copyright
        if ! grep -q "OpenSY developers" "$file" 2>/dev/null; then
            echo "NEEDS UPDATE (add OpenSY): $file"
            ((MODIFIED++)) || true
        fi
    fi
    
    # Check for old "Bitcoin Core" references that should be "OpenSY"
    if grep -q "The Bitcoin Core developers" "$file" 2>/dev/null; then
        # Files should reference OpenSY, not Bitcoin Core
        if ! grep -q "OpenSY developers" "$file" 2>/dev/null; then
            echo "NEEDS UPDATE (Bitcoin→OpenSY): $file"
            ((MODIFIED++)) || true
        fi
    fi
}

echo "Scanning source files..."
echo "========================"

# Find all C++ source and header files
while IFS= read -r -d '' file; do
    update_file "$file"
done < <(find src -type f \( -name "*.cpp" -o -name "*.h" \) -print0 2>/dev/null)

echo ""
echo "========================"
echo "Files needing updates: $MODIFIED"
echo ""
echo "To apply updates, edit each file to include:"
echo ""
echo "For original Bitcoin files (with Satoshi copyright):"
echo "  // Copyright (c) 2009-2010 Satoshi Nakamoto"
echo "  // Copyright (c) 2009-present The Bitcoin Core developers"
echo "  // Copyright (c) 2025-present The OpenSY developers"
echo ""
echo "For OpenSY-specific files:"
echo "  // Copyright (c) 2025-present The OpenSY developers"
