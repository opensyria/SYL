# OpenSY Upstream Sync Tracking

This document tracks which Bitcoin Core commits have been evaluated for incorporation into OpenSY.

## Current Upstream Base
- **Last Full Sync:** `3532e24213` (upstream/master as of 2026-02-03)
- **Last Review Date:** 2026-02-03

## Security Audit (2026-02-17)

A full security audit was performed against this upstream base. Key findings and
fixes applied:

| ID | Severity | Fix | Description |
|----|----------|-----|-------------|
| H-3 | High | Applied | `CheckBlockHeader` renamed to `CheckBlockHeaderStructure` to make the PoW-free no-op semantics explicit and prevent future contributors from creating zero-work acceptance paths |
| M-1 | Medium | Applied | Per-peer rate limiting (10/min) for RandomX serve-time PoW validation to prevent CPU DoS via excessive historical block requests |
| M-2 | Medium | Applied | Token disconnect moved from async `BlockDisconnected` signal handler into `DisconnectTip()` for atomic rollback with UTXO state |
| M-3 | Medium | Documented | Testnet `nMinimumChainWork` is empty — testnet at block 0 as of 2026-02-17, revisit once 1000+ blocks mined |
| L-1 | Low | Applied | Guard against `nRandomXKeyBlockInterval=0` (division by zero) in regtest configuration |
| L-2 | Low | Documented | `MAX_MONEY` has ~4.4x margin to `int64_t::max` (vs Bitcoin's 4385x) — documented for future contributors |
| L-5 | Low | Applied | Removed fake placeholder PGP key from SECURITY.md, replaced with clear instructions |
| — | — | Applied | All 17 token RPCs (12 node + 5 wallet) now include ADVISORY: non-consensus warning in help text |
| — | — | Applied | State-changing token RPCs (issue/transfer/burn × node+wallet = 6 RPCs) return runtime `warning` field in JSON response |
| — | — | Applied | Token roadmap and non-consensus advisory added to `doc/src20-spec.md` (Phases 1-4 + SIP design decisions) |

### Outstanding items requiring external action:
- **H-1 (DNS Seeds):** All 3 seeds controlled by single entity. Needs community-operated seeds on independent domains.
- **H-2 (Token State):** SRC-20 is non-consensus; state can diverge between node versions. Roadmap documented in `doc/src20-spec.md`. Phase 2 (deterministic validation) is the next step.
- **M-4 (xpub/xprv):** Extended key prefixes match Bitcoin — documented as intentional trade-off for hardware wallet compatibility.

## Commit Status Legend
- ✅ **APPLIED** - Cherry-picked into OpenSY
- ⏭️ **SKIPPED** - Reviewed and intentionally not applied
- 🔄 **PENDING** - Not yet reviewed

---

## Reviewed Commits (2026-02-03)

### Applied ✅

| Upstream Hash | Description | OpenSY Hash | Date |
|---------------|-------------|-------------|------|
| `fa41fc6a1a` | refactor: Operate on bytes instead of bits in Asmap code | `0c56c1f448` | 2026-02-03 |
| `385c34a052` | refactor: Unify asmap version calculation and naming | `8091936e02` | 2026-02-03 |
| `cf4943fdcd` | refactor: Use span instead of vector for data in util/asmap | `427b865d1e` | 2026-02-03 |
| `79e97d45c1` | doc: Add more extensive docs to asmap implementation | `e26262eb25` | 2026-02-03 |
| `4fec726c4d` | refactor: Simplify Interpret asmap function | `b9bf0058a9` | 2026-02-03 |
| `51abf7d15b` | script: remove unused SCRIPT_ERR_LAST | `1398d6382c` | 2026-02-03 |
| `facb2aab26` | test: Turn ElapseSteady into SteadyClockContext | `dc831bc80f` | 2026-02-03 |
| `dfb9364609` | fuzz: pull latest FuzzedDataProvider.h from upstream | `24e51ee8a4` | 2026-02-03 |
| `02b5f6078d` | fees: make flushes log debug only | `599de03ab4` | 2026-02-03 |

### Skipped ⏭️

| Upstream Hash | Description | Reason |
|---------------|-------------|--------|
| `3bd98b4508` | refactor: transparent comparator for setBlockIndexCandidates | Conflicts with OpenSY validation changes |
| `78df9003d6` | [doc] Update comments on dummy extraNonces in tests | IPC miner specific |
| `bf3b5d6d06` | test: clarify getCoinbaseRawTx() comparison | IPC miner specific |
| `d511adb664` | [miner] omit dummy extraNonce via IPC | IPC miner - OpenSY has custom mining |
| `57a778ed25` | depends: use -Xclang -fno-cxx-modules in macOS cross build | Depends system specific |
| `a89e1618dd` | contrib: update macOS SDK to Xcode-26.1.1-17B100 | SDK update - OpenSY uses different deps |
| `2222dadabb` | ci: [refactor] Allow overwriting check option | CI specific |
| `bbbb78a4f2` | ci: Print verbose build error message | CI specific |
| `580e9eefe3` | ci: bump CCACHE_MAXSIZE to 2G | CI specific |
| `3c8f5e48f7` | ci: Treat SHA1 LLVM signing key as warning | CI specific |
| `9d4c9b0035` | Squashed 'src/secp256k1/' changes | Subtree update |
| `1cee0e4cd3` | ci: detect apk usage generally | CI specific |
| `d405713197` | ci: use Alpine 3.23 | CI specific |
| `7528d18796` | ci: show more verbose ccache stats | CI specific |
| `2f2952c5f2` | Squashed 'src/leveldb/' changes | Subtree update |
| `fad7d86d8d` | ci: Remove unused workaround after leveldb bump | CI specific |
| Merge commits | Various | Merge commits don't cherry-pick |

---

## Reviewed Commits (2026-02-01)

### Applied ✅

| Upstream Hash | Description | OpenSY Hash | Date |
|---------------|-------------|-------------|------|
| `b189a34557` | test: add case where TOTAL_TRIES is exceeded | `638b54e300` | 2026-02-01 |
| `0ca4dcd786` | script: add SCRIPT_ERR_SCRIPTNUM error | `2988d94af6` | 2026-02-01 |
| `bd31a92d67` | script: use SCRIPT_ERR_SCRIPTNUM for CScriptNum errors | `eefda41a8a` | 2026-02-01 |
| `6f7b4323cb` | test: remove UNKNOWN_ERROR from script_tests | `d6ace8c08e` | 2026-02-01 |
| `fafdae46ff` | test: Check that redundant verack message is ignored | `becf64caf7` | 2026-02-01 |
| `4fab35cf88` | miniscript: correct and_v() properties | `cea1ebabcc` | 2026-02-01 |
| `eeb4d28148` | validation: follow-up nits for lock-free IsInitialBlockDownload() | `087c3efe56` | 2026-02-01 |
| `d3e681bc06` | fuzz: Use __AFL_SHM_ID for naming test directories | `6128312bba` | 2026-02-01 |
| `516be10bb5` | wallet: Rename RecordType::DELETE to DELETE_FLAG | `f9c88a3e6b` | 2026-02-01 |
| `07af50f789` | util: Drop *BSD headers in batchpriority.cpp | `8ab1ef631c` | 2026-02-01 |
| `22e4115312` | doc(miniscript): Remove mention of shared pointers | `cdf01a1910` | 2026-02-01 |

### Skipped ⏭️

| Upstream Hash | Description | Reason |
|---------------|-------------|--------|
| `0067abe153` | p2p: Allow block downloads after assumeutxo validation | Requires m_assumeutxo enum (missing dependency) |
| `7d9e1a8102` | test: Verify peer usage after assumeutxo validation | Depends on 0067abe153 |
| `552bc82b17` | doc: Use multipath descriptors in descriptors.md | File deleted in OpenSY |
| `7099e93d0a` | refactor: rename FlushStateMode::ALWAYS to FORCE_FLUSH | Part of coins cache series, conflicts with tracing removal |
| `c6ca2b85a3` | validation: do not wipe utxo cache | Depends on FORCE_FLUSH refactor |
| `8dd9200fc9` | coins: add Reset on CCoinsViewCache | Depends on prior coins commits |
| `041758f5ed` | coins: use hashBlock setter internally | Part of coins cache series |
| `8fb6043231` | coins: introduce CCoinsViewCache::ResetGuard | Part of coins cache series |
| `44b4ee194d` | validation: reuse same CCoinsViewCache | Part of coins cache series |
| `3e0fd0e4dd` | refactor: rename will_reuse_cache to reallocate_cache | Part of coins cache series |
| `c6f798b222` | refactor(miniscript): Make fields non-const & private | Large refactor with conflicts |
| `e55b23c170` | refactor(miniscript): Remove Node::subs mutability | Depends on prior miniscript refactor |
| `15fb34de41` | refactor(miniscript): Remove unique_ptr-indirection | Depends on prior miniscript refactor |
| `50cab8570e` | refactor(miniscript): Remove NodeRef & MakeNodeRef() | Depends on prior miniscript refactor |
| `198bbaee49` | refactor(miniscript): Destroy nodes one full subs-vector | Depends on prior miniscript refactor |
| `964c44cdcd` | test(miniscript): Prove avoidance of stack overflow | Depends on prior miniscript refactor |
| `efcbf79448` | ci, iwyu: Fix warnings in src/zmq | OpenSY CI is different |
| `fad2876ec3` | ci: Always print low ccache hit rate notice | OpenSY CI is different |
| `37de7d1910` | iwyu: Drop backported mapping | IWYU not used in OpenSY |
| `91824646c5` | iwyu: Add temporary mapping | IWYU not used in OpenSY |
| `9c839aa9e3` | iwyu: Document mappings for libc symbols | IWYU not used in OpenSY |
| `1bf3842223` | ci, iwyu: Fix warnings in src/univalue | OpenSY CI is different |
| Merge commits | Various | Merge commits don't cherry-pick |

---

## Reviewed Commits (2026-01-29)

### Applied ✅

| Upstream Hash | Description | OpenSY Hash | Date |
|---------------|-------------|-------------|------|
| `ee1e40f580` | txdb: assert CCoinsViewDB::GetCoin only returns unspent coins | `d221a86e61` | 2026-01-29 |
| `3e4155fcef` | test: do not return spent coins from CCoinsViewTest::GetCoin | `481035fb06` | 2026-01-29 |
| `eec551aaf1` | fuzz: keep coinscache_sim backend free of spent coins | `f7cc7ed535` | 2026-01-29 |
| `2ee7f9b259` | coins: assume GetCoin only returns unspent coins | `50f955f55c` | 2026-01-29 |
| `8be54e3b19` | test: cover IBD exit conditions | `5bdb193a7b` | 2026-01-29 |
| `8d531c6210` | validation: invert m_cached_finished_ibd to m_cached_is_ibd | `6b04352625` | 2026-01-29 |
| `b9c0ab3b75` | chain: add CChain::IsTipRecent helper | `c1e1961749` | 2026-01-29 |
| `557b41a38c` | validation: make IsInitialBlockDownload() lock-free | `31c9fd90e5` | 2026-01-29 |
| `1f60ca360e` | wallet: fix removeprunedfunds bug with conflicting transactions | `7e7f05536e` | 2026-01-29 |
| `e770392084` | test: addrman: test self-announcement time penalty handling | `216edb9e41` | 2026-01-29 |

### Skipped ⏭️

| Upstream Hash | Description | Reason |
|---------------|-------------|--------|
| `34bed0ed8c` | test: use IP_PORTRANGE_HIGH on FreeBSD | FreeBSD-specific, not applicable |
| `2845f10a2b` | test: extend FreeBSD ephemeral port range fix | FreeBSD-specific, not applicable |
| `fad042235b` | refactor: Remove remaining std::bind | Touches qt/ files, may conflict with OpenSY GUI changes |
| Merge commits | Various | Merge commits don't cherry-pick |

---

## Reviewed Commits (2026-01-26)

### Applied ✅

| Upstream Hash | Description | OpenSY Hash | Date |
|---------------|-------------|-------------|------|
| `faa5a9ebad` | fuzz: Use min option in ConsumeTime | `6d75d91a71` | 2026-01-26 |
| `eeee3755f8` | fuzz: Return chrono point from ConsumeTime(), Add ConsumeDuration() | `0ba7638db8` | 2026-01-26 |
| `a73a3ec553` | doc: fix invalid arg name hints for bugprone validation | `82308c431f` | 2026-01-26 |

### Skipped ⏭️

| Upstream Hash | Description | Reason |
|---------------|-------------|--------|
| `fa37928536` | build: Use debug-prefix-map for cross builds | Requires guix directory (removed in OpenSY) |
| `fa15a8d2d0` | doc: Update CONTRIBUTING.md | OpenSY has its own contributing guidelines |
| `b261100e71` | util: Remove FilterHeaderHasher | Already applied in earlier sync |
| `be2b48b9f3` | (related to FilterHeaderHasher) | Already applied in earlier sync |
| Merge commits | Various | Merge commits don't cherry-pick |

---

## Categories to Always Skip

1. **Branding/Documentation** - OpenSY has its own identity
2. **Guix/Reproducible Builds** - Directory removed in OpenSY
3. **GitHub CI Workflows** - OpenSY uses different CI
4. **Merge Commits** - Not applicable for cherry-picking

## Categories to Always Review

1. **Security Fixes** - Critical to apply
2. **Consensus/Validation** - Must evaluate carefully
3. **P2P/Network** - Usually applicable
4. **Wallet** - Usually applicable
5. **RPC** - Usually applicable
6. **Fuzz/Test Improvements** - Usually applicable

---

## How to Sync

```bash
# 1. Fetch upstream
git fetch upstream

# 2. List new commits since last sync
git log --oneline <LAST_SYNC_HASH>..upstream/master

# 3. Review each commit
git show <hash> --stat

# 4. Cherry-pick applicable ones
git cherry-pick <hash>

# 5. Update this document!

# 6. Rebuild and test
rm -rf build && mkdir build && cd build
cmake .. -DBUILD_GUI=OFF -DWITH_ZMQ=OFF
cmake --build . -j$(sysctl -n hw.ncpu)
./bin/test_opensy
```

---

## Sync History

### 2026-01-28
- Reviewed 23 non-merge commits from `c0e6556e4f` to `2dd5e7bb38`
- Applied: 5 commits
  - `fa9c92d7b6` log: Print warning about privacy-sensitive log info unconditionally
  - `905dfdee86` test: use ModuleNotFoundError in interface_ipc.py
  - `477c5504e0` coins: replace std::distance with unambiguous pointer subtraction
  - `fad7bd9ba3` noui: Remove always empty caption while formatting
  - `eeee1e341f` refactor: Remove trailing semicolon after ADD_SIGNALS_DECL_WRAPPER
- Skipped:
  - `c8abac9941` ci: Skip clang-tidy for functional tests - CI related
  - `ddae1b4efa` ci: add libfontconfig1 - CI related
  - `fdc9fe2da6` ci: add Windows cross native lib deps - CI related
  - `5aeaa71c77` lint: upgrade pyzmq to 27.0.0 - lint tooling
  - `c17a2adb8d` lint: require mypy>=1.16.0 - lint tooling
  - `fa578d9434` lint: Use ruff check as linter - lint tooling
  - `faf40c2f84` lint: Use ruff format as formatter - lint tooling
  - `ab649ce459` guix: Update time-machine to noble - guix removed in OpenSY
  - `2fccbea3c8` Merge bitcoin-core/secp256k1#1762 - submodule merge
  - `fa8ebeb332` refactor: Move ThreadSafeMessageBox to interface_ui - GUI conflicts
  - `fa0195499c` gui: Use bilingual_str for ThreadSafeMessageBox - GUI conflicts
  - `fafe71b743` refactor: Move ThreadSafeMessageBox to AppInitMain - too many conflicts
  - `fa8d0088e7` refactor: Move ThreadSafeQuestion to interface_ui - too many conflicts
  - All merge commits

### 2026-01-27
- Reviewed 31 commits from `34a5ecadd7` to `c0e6556e4f`
- Applied: 8 commits
  - `e71c4df168` refactor: replace manual promise with SyncWithValidationInterfaceQueue
  - `c7028d3368` init: log that additional logs may contain privacy-sensitive information
  - `b39291f4cd` doc: fix -logips description
  - `a9440b1595` util: add TicksSeconds
  - `14f99cfe53` rpc: make uptime monotonic across NTP jumps
  - `81675a781f` test: use pre-generated chain
  - `7cfe790820` test: replace ValidWitnessMalleatedTx class with function
  - `3f5211cba8` test: remove child_one/child_two (w)txid variables
- Skipped:
  - `31b771a942` net: move privatebroadcast logs - conflicts with OpenSY net changes
  - `fa06cd4ba7` doc: developer-notes.md changes - OpenSY has own docs
  - `fa2e1b85dd` build: comment removal - minimal value
  - All merge commits

### 2026-01-26
- Reviewed 15 commits from `5b8c204275` to `34a5ecadd7`
- Applied: 3 commits (fuzz improvements, doc fixes)
- Skipped: 12 commits (merge commits, guix, already applied, branding)

