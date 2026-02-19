# Checkpoint Policy & Procedures

**Document:** OpenSY Chain Checkpoints  
**Version:** 2.0  
**Last Updated:** February 2025

---

## Overview

Checkpoints are hard-coded height → block-hash entries in `src/kernel/chainparams.cpp`.
When `AcceptBlockHeader()` receives a header whose height matches a checkpoint entry,
it verifies the header hash matches.  A mismatch causes rejection with reason
`checkpoint-mismatch` (`BlockValidationResult::BLOCK_CHECKPOINT`).

Checkpoints **complement** (not replace) the modern security mechanisms already in place:

| Mechanism | Purpose | Location |
|-----------|---------|----------|
| `m_checkpoints` | Reject headers that diverge at known heights | `CChainParams` |
| `nMinimumChainWork` | Reject chains with less total PoW | `Consensus::Params` |
| `defaultAssumeValid` | Skip script verification for known-good blocks | `Consensus::Params` |
| `m_assumeutxo_data` | Snapshot-based fast sync | `CChainParams` |

---

## Current Checkpoints

### Mainnet

```cpp
// src/kernel/chainparams.cpp — CMainParams::m_checkpoints
m_checkpoints = {
    {      0, uint256{"000000c4c94f54e5ae60a67df5c113dfbfd9ef872639e2359d15796f27920fd1"}},
    {  50000, uint256{"000000d308772cf89715e4386cf4581f4c16f4a1e102847074a21bb96745916b"}},
    { 100000, uint256{"0000006319a52f1a332b32157b69e887691ffb1b914f1470e0768886c34a1aec"}},
    { 150000, uint256{"0000006f37e730f314815973cbb2989c9e7fb60ae1c908e203681076f265a634"}},
    { 200000, uint256{"00000050100b66de8aaa831b90f761d95ab11d0420103a31c9a90ac74655a560"}},
    { 210000, uint256{"1e0eb2fa9f55e6818e9109bf6316486f2de8072dd96a682d133bc72f734da4e5"}},
};
```

| Height | Hash | Notes |
|--------|------|-------|
| 0 | `000000c4...920fd1` | Genesis block (Dec 8 2024) |
| 50,000 | `000000d3...45916b` | Early-chain anchor |
| 100,000 | `00000063...a1aec` | Mid-Phase-1 anchor |
| 150,000 | `0000006f...a634` | Late-Phase-1 anchor |
| 200,000 | `00000050...a560` | Pre-RandomX transition |
| 210,000 | `1e0eb2fa...a4e5` | RandomX activation (Phase 2) |

---

## Checkpoint Selection Criteria

A block qualifies as a checkpoint candidate when:

1. ✅ **Age:** At least 10,000 confirmations (~2 weeks)
2. ✅ **Stability:** No known reorgs affecting this height
3. ✅ **Significance:** Milestone blocks (round numbers, forks, events)
4. ✅ **Verification:** Hash verified by multiple independent nodes

### Recommended Checkpoint Heights

| Block Height | Approximate Date | Significance |
|--------------|------------------|--------------|
| 100,000 | Month ~5 | Early adoption milestone |
| 210,000 | Month ~10 | RandomX activation |
| 500,000 | Month ~23 | First anniversary milestone |
| 1,050,000 | Year ~4 | First halving |

---

## Adding New Checkpoints

### 1. Obtain Block Hash

```bash
# Get hash at specific height
opensy-cli getblockhash 100000

# Verify with block details
opensy-cli getblock $(opensy-cli getblockhash 100000)
```

### 2. Verify Independently

Confirm the hash with:
- Multiple full nodes in different locations
- Block explorer (explorer.opensyria.net)
- Community verification

### 3. Update Code

File: `src/kernel/chainparams.cpp`

```cpp
m_checkpoints = {
    // ... existing entries ...
    { 100000, uint256{"0000006319a52f1a332b32157b69e887691ffb1b914f1470e0768886c34a1aec"}},
    { NEW_HEIGHT, uint256{"NEW_BLOCK_HASH_HERE"}},
};
```

### 4. Update Assumed Blockchain State

Also update in the same file:

```cpp
m_assumed_blockchain_size = XX;  // Estimated GB at checkpoint
m_assumed_chain_state_size = YY; // Estimated chainstate GB
```

### 5. Release Process

1. Create PR with checkpoint addition
2. Require 2+ maintainer approvals
3. Include in next minor version release
4. Announce checkpoint update to network

---

## Checkpoint Governance

### Decision Authority

Checkpoints are added by:
- Core maintainers with 2+ approvals
- Community review period (48 hours minimum)

### Emergency Checkpoints

In case of active attack:
1. Verify attack with network operators
2. Select checkpoint before attack block
3. Fast-track release with security notice
4. Coordinate upgrade across major nodes

### Checkpoint Controversy

If checkpoint is disputed:
1. Pause release
2. Gather evidence from all parties
3. Community vote if necessary
4. Document decision rationale

---

## Technical Details

### How Checkpoints Work

```cpp
// In validation.cpp — ChainstateManager::AcceptBlockHeader()
const int nHeight = pindexPrev->nHeight + 1;
const auto& checkpoints = GetParams().Checkpoints();
auto it = checkpoints.find(nHeight);
if (it != checkpoints.end() && hash != it->second) {
    return state.Invalid(BlockValidationResult::BLOCK_CHECKPOINT,
                         "checkpoint-mismatch");
}
```

### Performance Impact

- Blocks **at** a checkpoint height: header hash verified against the hardcoded value
- Blocks at **all** heights: full PoW and contextual validation still applies
- Script verification is separately optimised by `defaultAssumeValid`

### Storage Location

Checkpoints are stored in:
- `src/kernel/chainparams.cpp` → `CMainParams::m_checkpoints` (hard-coded)
- Accessor: `CChainParams::Checkpoints()` (returns `const std::map<int, uint256>&`)
- NOT configurable via command-line or config file

---

## Security Considerations

### What Checkpoints Prevent

1. **Long-range attacks** - Cannot reorg before checkpoint
2. **Alternative history** - Must include checkpoint blocks
3. **Eclipse attacks** - Nodes reject chains missing checkpoints

### What Checkpoints Don't Prevent

1. **51% attacks after checkpoint** - Only history is protected
2. **Soft forks** - Checkpoints don't enforce soft fork rules
3. **Node compromise** - Checkpoints are in code, not consensus

### Checkpoint Risks

1. **Centralization** - Maintainers choose checkpoints
2. **Contentious forks** - Checkpoint can pick "winner"
3. **Delayed updates** - Old nodes miss new checkpoints

---

## Best Practices

### For Node Operators

1. Keep software updated for latest checkpoints
2. Verify checkpoint hashes independently before trusting
3. Report suspicious blocks to security team

### For Maintainers

1. Add checkpoints conservatively (not too frequently)
2. Allow sufficient confirmation time (10,000+ blocks)
3. Document all checkpoint additions in CHANGELOG
4. Never add checkpoints to resolve contentious forks

---

## Verification Script

Verify current checkpoints against network:

```bash
#!/bin/bash
# verify_checkpoints.sh

CHECKPOINTS=(
    "0:000000c4c94f54e5ae60a67df5c113dfbfd9ef872639e2359d15796f27920fd1"
    "50000:000000d308772cf89715e4386cf4581f4c16f4a1e102847074a21bb96745916b"
    "100000:0000006319a52f1a332b32157b69e887691ffb1b914f1470e0768886c34a1aec"
    "150000:0000006f37e730f314815973cbb2989c9e7fb60ae1c908e203681076f265a634"
    "200000:00000050100b66de8aaa831b90f761d95ab11d0420103a31c9a90ac74655a560"
    "210000:1e0eb2fa9f55e6818e9109bf6316486f2de8072dd96a682d133bc72f734da4e5"
)

for cp in "${CHECKPOINTS[@]}"; do
    HEIGHT="${cp%%:*}"
    EXPECTED="${cp##*:}"
    ACTUAL=$(opensy-cli getblockhash $HEIGHT 2>/dev/null)
    
    if [ "$ACTUAL" = "$EXPECTED" ]; then
        echo "✅ Height $HEIGHT: MATCH"
    else
        echo "❌ Height $HEIGHT: MISMATCH"
        echo "   Expected: $EXPECTED"
        echo "   Actual:   $ACTUAL"
    fi
done
```

---

## Changelog

| Date | Height | Hash | Added By | Notes |
|------|--------|------|----------|-------|
| Dec 2024 | 0 | `000000c4...` | Genesis | Network launch |
| Feb 2025 | 50,000 | `000000d3...` | Audit | Early-chain anchor |
| Feb 2025 | 100,000 | `00000063...` | Audit | Mid-Phase-1 anchor |
| Feb 2025 | 150,000 | `0000006f...` | Audit | Late-Phase-1 anchor |
| Feb 2025 | 200,000 | `00000050...` | Audit | Pre-RandomX transition |
| Feb 2025 | 210,000 | `1e0eb2fa...` | Audit | RandomX activation |

---

*For checkpoint additions or disputes, open a GitHub issue or contact security@opensyria.net*
