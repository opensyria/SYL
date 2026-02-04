# Checkpoint Policy & Procedures

**Document:** OpenSY Chain Checkpoints  
**Version:** 1.0  
**Last Updated:** January 2025

---

## Overview

Checkpoints are hard-coded block hashes that help nodes verify they are on the correct chain. They provide:

1. **Security** - Prevent deep reorganization attacks
2. **Performance** - Skip signature verification for blocks before checkpoint
3. **Consistency** - Ensure all nodes agree on chain history

---

## Current Checkpoints

### Mainnet

```cpp
// src/kernel/chainparams.cpp - CMainParams
checkpointData = {
    {
        {0, uint256S("000000c4c94f54e5ae60a67df5c113dfbfd9ef872639e2359d15796f27920fd1")},
        // Add checkpoints at major milestones
    }
};
```

| Height | Hash | Date | Notes |
|--------|------|------|-------|
| 0 | `000000c4...920fd1` | Dec 8, 2024 | Genesis Block |

### Testnet

```cpp
checkpointData = {
    {
        {0, uint256S("<testnet_genesis_hash>")},
    }
};
```

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
checkpointData = {
    {
        {0, uint256S("000000c4c94f54e5ae60a67df5c113dfbfd9ef872639e2359d15796f27920fd1")},
        {100000, uint256S("NEW_BLOCK_HASH_HERE")},
    }
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
// In validation.cpp
bool CheckBlock(const CBlock& block, ...)
{
    // If block height matches checkpoint, verify hash
    if (checkpointData.contains(height)) {
        if (block.GetHash() != checkpointData[height]) {
            return false;  // Reject block
        }
    }
    // ...
}
```

### Performance Impact

- Blocks **before** last checkpoint: Skip signature verification (faster sync)
- Blocks **at** checkpoint: Verify hash matches exactly
- Blocks **after** checkpoint: Full verification

### Storage Location

Checkpoints are stored in:
- `src/kernel/chainparams.cpp` (hard-coded)
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
    # Add more as needed
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

---

*For checkpoint additions or disputes, open a GitHub issue or contact security@opensyria.net*
