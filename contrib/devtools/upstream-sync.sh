#!/usr/bin/env bash
# OpenSY Upstream Sync Helper
#
# This script helps synchronize commits from Bitcoin Core upstream.
# Supports both autonomous and interactive modes.
#
# Usage:
#   ./contrib/devtools/upstream-sync.sh                    # Interactive mode
#   ./contrib/devtools/upstream-sync.sh --list             # List new commits  
#   ./contrib/devtools/upstream-sync.sh --auto             # Autonomous mode (safe commits only)
#   ./contrib/devtools/upstream-sync.sh --cherry <hash>    # Cherry-pick specific commit
#   ./contrib/devtools/upstream-sync.sh --analyze          # Analyze all commits with recommendations
#
# Prerequisites:
#   - Remote 'upstream' pointing to bitcoin/bitcoin
#   - Clean working directory
#
# Setup (one-time):
#   git remote add upstream https://github.com/bitcoin/bitcoin.git

set -e

# Configuration
UPSTREAM_REMOTE="upstream"
UPSTREAM_BRANCH="master"
SYNC_DOC="doc/upstream-sync.md"

# Patterns to auto-skip (never cherry-pick)
SKIP_PATTERNS=(
    "^Merge"                    # Merge commits
    "guix"                      # Guix build system (removed)
    "ci:"                       # GitHub CI workflows  
    "depends:"                  # Depends system changes
    "doc: Update.*README"       # Bitcoin-specific docs
    "CONTRIBUTING"              # Bitcoin contributing guidelines
    "autoconf\|automake\|configure.ac"  # Autotools (removed)
)

# Files that OpenSY has significantly modified (require manual review)
OPENSY_MODIFIED_FILES=(
    "src/pow.cpp"
    "src/pow.h"
    "src/kernel/chainparams.cpp"
    "src/chainparams.cpp"
    "src/consensus/params.h"
    "src/validation.cpp"
    "src/miner.cpp"
    "src/rpc/mining.cpp"
    "src/wallet/tokens"
    "CMakeLists.txt"
)

# Files/dirs that OpenSY has deleted (auto-skip commits touching only these)
OPENSY_DELETED=(
    "configure.ac"
    "Makefile.am"
    "src/Makefile"
    "autogen.sh"
    "build-aux/"
    "contrib/guix"
    "contrib/gitian"
)

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Check prerequisites
check_prerequisites() {
    # Check if upstream remote exists
    if ! git remote | grep -q "^${UPSTREAM_REMOTE}$"; then
        log_error "Remote '${UPSTREAM_REMOTE}' not found!"
        echo "Add it with: git remote add ${UPSTREAM_REMOTE} https://github.com/bitcoin/bitcoin.git"
        exit 1
    fi
    
    # Check for clean working directory
    if ! git diff-index --quiet HEAD --; then
        log_error "Working directory is not clean. Commit or stash changes first."
        exit 1
    fi
    
    log_info "Fetching from ${UPSTREAM_REMOTE}..."
    git fetch "${UPSTREAM_REMOTE}" --quiet
}

# Get the last synced commit hash from the sync doc
get_last_sync() {
    if [[ -f "${SYNC_DOC}" ]]; then
        # Try multiple patterns to find the hash
        grep -oE 'Last Full Sync:\*?\*? `[a-f0-9]+' "${SYNC_DOC}" 2>/dev/null | grep -oE '[a-f0-9]{7,}' | head -1 || echo ""
    fi
}

# Check if a commit should be auto-skipped based on message
should_skip_message() {
    local message="$1"
    for pattern in "${SKIP_PATTERNS[@]}"; do
        if echo "$message" | grep -qE "$pattern"; then
            return 0
        fi
    done
    return 1
}

# Check if commit only touches deleted files (safe to skip)
touches_only_deleted() {
    local hash="$1"
    local files
    files=$(git show --name-only --format="" "$hash")
    
    for file in $files; do
        local is_deleted=false
        for deleted in "${OPENSY_DELETED[@]}"; do
            if [[ "$file" == "$deleted"* ]]; then
                is_deleted=true
                break
            fi
        done
        if [[ "$is_deleted" == "false" ]]; then
            # This file is NOT in deleted list - commit touches live files
            return 1
        fi
    done
    return 0  # All files are deleted
}

# Check if commit touches OpenSY-modified files (needs review)
touches_modified_files() {
    local hash="$1"
    local files
    files=$(git show --name-only --format="" "$hash")
    
    for file in $files; do
        for modified in "${OPENSY_MODIFIED_FILES[@]}"; do
            if [[ "$file" == *"$modified"* ]]; then
                return 0  # Touches modified file
            fi
        done
    done
    return 1  # Doesn't touch any modified files
}

# Analyze a commit and return: SAFE, SKIP, REVIEW, or RISKY
analyze_commit() {
    local hash="$1"
    local message
    message=$(git log --format="%s" -1 "$hash")
    
    # Check message patterns first
    if should_skip_message "$message"; then
        echo "SKIP"
        return
    fi
    
    # Check if it only touches deleted files
    if touches_only_deleted "$hash"; then
        echo "SKIP_DELETED"
        return
    fi
    
    # Check if it touches OpenSY-modified files
    if touches_modified_files "$hash"; then
        echo "RISKY"
        return
    fi
    
    # Check file types - tests and docs are usually safe
    local files
    files=$(git show --name-only --format="" "$hash")
    local all_safe=true
    
    for file in $files; do
        case "$file" in
            test/*|src/test/*|doc/*|*.md)
                # Safe categories
                ;;
            src/bench/*|src/fuzz/*)
                # Benchmarks and fuzz tests - usually safe
                ;;
            *)
                all_safe=false
                ;;
        esac
    done
    
    if [[ "$all_safe" == "true" ]]; then
        echo "SAFE"
    else
        echo "REVIEW"
    fi
}

# List new commits since last sync
list_new_commits() {
    local last_sync="$1"
    local count=0
    local skip_count=0
    local safe_count=0
    local risky_count=0
    
    echo ""
    echo "==========================================================="
    echo "  NEW COMMITS SINCE LAST SYNC"
    echo "==========================================================="
    echo ""
    
    while IFS= read -r line; do
        hash=$(echo "$line" | cut -d' ' -f1)
        message=$(echo "$line" | cut -d' ' -f2-)
        status=$(analyze_commit "$hash")
        
        case "$status" in
            SKIP|SKIP_DELETED)
                ((skip_count++)) || true
                echo -e "  ${YELLOW}[SKIP]${NC}   $hash $message"
                ;;
            SAFE)
                ((safe_count++)) || true
                echo -e "  ${GREEN}[SAFE]${NC}   $hash $message"
                ;;
            RISKY)
                ((risky_count++)) || true
                echo -e "  ${RED}[RISKY]${NC}  $hash $message"
                ;;
            *)
                ((count++)) || true
                echo -e "  ${BLUE}[REVIEW]${NC} $hash $message"
                ;;
        esac
    done < <(git log --oneline "${last_sync}..${UPSTREAM_REMOTE}/${UPSTREAM_BRANCH}" --reverse 2>/dev/null)
    
    echo ""
    echo "==========================================================="
    echo "  Summary:"
    echo "    ${GREEN}SAFE (auto-apply OK):${NC} $safe_count"
    echo "    ${BLUE}REVIEW (needs look):${NC}  $count"
    echo "    ${RED}RISKY (touches OpenSY code):${NC} $risky_count"
    echo "    ${YELLOW}SKIP (auto-skipped):${NC}  $skip_count"
    echo "==========================================================="
}

# Cherry-pick a specific commit
cherry_pick_commit() {
    local hash="$1"
    
    log_info "Cherry-picking $hash..."
    
    # Show the commit details
    echo ""
    git show --stat "$hash"
    echo ""
    
    read -p "Proceed with cherry-pick? [y/N/s(skip)] " -n 1 -r
    echo
    
    case "$REPLY" in
        y|Y)
            if git cherry-pick "$hash"; then
                log_success "Successfully cherry-picked $hash"
                echo ""
                echo "Don't forget to update ${SYNC_DOC}!"
                return 0
            else
                log_error "Cherry-pick failed. Resolve conflicts and run: git cherry-pick --continue"
                return 1
            fi
            ;;
        s|S)
            log_warn "Skipped $hash"
            echo "Remember to document in ${SYNC_DOC} as SKIPPED"
            return 0
            ;;
        *)
            log_info "Aborted"
            return 1
            ;;
    esac
}

# Interactive mode
interactive_mode() {
    local last_sync
    last_sync=$(get_last_sync)
    
    if [[ -z "$last_sync" ]]; then
        log_warn "Could not determine last sync from ${SYNC_DOC}"
        read -p "Enter the last synced commit hash: " last_sync
    fi
    
    log_info "Last synced commit: $last_sync"
    list_new_commits "$last_sync"
    
    echo ""
    echo "Commands:"
    echo "  ./contrib/devtools/upstream-sync.sh --cherry <hash>  # Cherry-pick a commit"
    echo "  git show <hash>                                       # View commit details"
    echo "  git log --oneline <hash>~5..<hash>                    # View context"
}

# Update sync doc with new timestamp
update_sync_doc() {
    local new_hash="$1"
    local today
    today=$(date +%Y-%m-%d)
    
    if [[ -f "${SYNC_DOC}" ]]; then
        # Update the last sync line
        sed -i.bak "s/Last Full Sync:\*\* \`[a-f0-9]*\`/Last Full Sync:** \`${new_hash}\`/" "${SYNC_DOC}"
        sed -i.bak "s/Last Review Date:\*\* [0-9-]*/Last Review Date:** ${today}/" "${SYNC_DOC}"
        rm -f "${SYNC_DOC}.bak"
        log_success "Updated ${SYNC_DOC}"
    fi
}

# Autonomous mode - auto-apply safe commits, skip unsafe ones
autonomous_mode() {
    local last_sync
    last_sync=$(get_last_sync)
    local applied=0
    local skipped=0
    local needs_review=0
    local failed=0
    
    if [[ -z "$last_sync" ]]; then
        log_error "Could not determine last sync from ${SYNC_DOC}"
        exit 1
    fi
    
    log_info "Starting autonomous sync from $last_sync..."
    echo ""
    
    while IFS= read -r line; do
        hash=$(echo "$line" | cut -d' ' -f1)
        message=$(echo "$line" | cut -d' ' -f2-)
        status=$(analyze_commit "$hash")
        
        case "$status" in
            SKIP|SKIP_DELETED)
                echo -e "  ${YELLOW}[SKIP]${NC} $hash - $message"
                ((skipped++)) || true
                ;;
            SAFE)
                echo -e "  ${GREEN}[AUTO-APPLY]${NC} $hash - $message"
                if git cherry-pick --no-commit "$hash" 2>/dev/null; then
                    # Check if it actually made changes
                    if git diff --cached --quiet; then
                        echo "    (no changes, already applied?)"
                        git cherry-pick --abort 2>/dev/null || true
                    else
                        git commit -m "$message" -m "Cherry-picked from bitcoin/bitcoin@$hash"
                        ((applied++)) || true
                        log_success "Applied $hash"
                    fi
                else
                    log_warn "Failed to apply $hash (conflict?). Skipping."
                    git cherry-pick --abort 2>/dev/null || true
                    ((failed++)) || true
                fi
                ;;
            RISKY)
                echo -e "  ${RED}[RISKY - SKIP]${NC} $hash - $message"
                echo "    Touches: $(git show --name-only --format="" "$hash" | tr '\n' ' ')"
                ((needs_review++)) || true
                ;;
            *)
                echo -e "  ${BLUE}[NEEDS REVIEW]${NC} $hash - $message"
                ((needs_review++)) || true
                ;;
        esac
    done < <(git log --oneline "${last_sync}..${UPSTREAM_REMOTE}/${UPSTREAM_BRANCH}" --reverse 2>/dev/null)
    
    echo ""
    echo "==========================================================="
    echo "  AUTONOMOUS SYNC COMPLETE"
    echo "==========================================================="
    echo "  Applied:      $applied"
    echo "  Skipped:      $skipped"
    echo "  Failed:       $failed"
    echo "  Needs Review: $needs_review"
    echo "==========================================================="
    
    if [[ $needs_review -gt 0 ]]; then
        echo ""
        log_warn "$needs_review commits need manual review. Run:"
        echo "  ./contrib/devtools/upstream-sync.sh --list"
    fi
    
    if [[ $applied -gt 0 ]]; then
        echo ""
        log_info "Don't forget to run tests: ctest --test-dir build"
    fi
}

# Main
main() {
    cd "$(git rev-parse --show-toplevel)"
    
    case "${1:-}" in
        --list)
            check_prerequisites
            local last_sync
            last_sync=$(get_last_sync)
            if [[ -z "$last_sync" ]]; then
                log_error "Could not determine last sync"
                exit 1
            fi
            list_new_commits "$last_sync"
            ;;
        --auto)
            check_prerequisites
            autonomous_mode
            ;;
        --analyze)
            check_prerequisites
            local last_sync
            last_sync=$(get_last_sync)
            if [[ -z "$last_sync" ]]; then
                log_error "Could not determine last sync"
                exit 1
            fi
            list_new_commits "$last_sync"
            ;;
        --cherry)
            if [[ -z "${2:-}" ]]; then
                log_error "Usage: $0 --cherry <commit-hash>"
                exit 1
            fi
            check_prerequisites
            cherry_pick_commit "$2"
            ;;
        --update-doc)
            if [[ -z "${2:-}" ]]; then
                log_error "Usage: $0 --update-doc <new-sync-hash>"
                exit 1
            fi
            update_sync_doc "$2"
            ;;
        --help|-h)
            echo "OpenSY Upstream Sync Helper"
            echo ""
            echo "Usage:"
            echo "  $0                     Interactive mode (default)"
            echo "  $0 --list              List new upstream commits with analysis"
            echo "  $0 --auto              AUTONOMOUS: auto-apply safe commits"
            echo "  $0 --cherry <hash>     Cherry-pick a specific commit"
            echo "  $0 --update-doc <hash> Update sync doc with new hash"
            echo "  $0 --help              Show this help"
            echo ""
            echo "Commit Categories:"
            echo "  [SAFE]   - Tests/docs only, auto-apply OK"
            echo "  [REVIEW] - General changes, needs human review"
            echo "  [RISKY]  - Touches OpenSY-modified files (pow, chainparams)"
            echo "  [SKIP]   - Auto-skipped (merge commits, deleted systems)"
            ;;
        *)
            check_prerequisites
            interactive_mode
            ;;
    esac
}

main "$@"
