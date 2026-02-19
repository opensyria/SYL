# Emergency PoW Activation Runbook

**Document:** OpenSY Emergency Procedures  
**Version:** 1.0  
**Classification:** Operator Reference  
**Last Updated:** January 2025

---

## Overview

OpenSY uses a three-phase Proof-of-Work system:

| Phase | Algorithm | Block Range | Status |
|-------|-----------|-------------|--------|
| 1 | SHA256d | 0 - 209,999 | ✅ Active (bootstrapping) |
| 2 | RandomX | 210,000+ | ⏳ Pending |
| 3 | Argon2id | Emergency | 💤 Dormant |

This document covers the emergency activation of **Argon2id** fallback if RandomX is compromised.

---

## When to Activate Argon2id

Activate emergency PoW **only** when:

1. ✅ **Confirmed RandomX vulnerability** - Published CVE or demonstrated exploit
2. ✅ **Active exploitation** - Evidence of 51% attack or hashrate manipulation
3. ✅ **Community consensus** - Supermajority of nodes/miners agree
4. ✅ **No viable RandomX patch** - Vulnerability cannot be fixed quickly

**DO NOT** activate for:
- ❌ Temporary hashrate fluctuations
- ❌ Individual miner complaints
- ❌ Unverified vulnerability reports
- ❌ Network congestion (not a PoW issue)

---

## Pre-Activation Checklist

Before initiating emergency activation:

### 1. Verify the Threat

```bash
# Check current network hashrate
opensy-cli getmininginfo

# Verify block production is normal
opensy-cli getblockchaininfo

# Check for unusual reorg depth
opensy-cli getchaintips
```

### 2. Coordinate with Community

- [ ] Post to official channels (Telegram, Twitter, GitHub)
- [ ] Collect acknowledgments from major node operators
- [ ] Document the specific threat being addressed
- [ ] Set activation block height (at least 1,008 blocks in future = ~1.4 days)

### 3. Prepare Release

- [ ] Create tagged release with Argon2id activation
- [ ] Test on signet/testnet first
- [ ] Generate reproducible builds
- [ ] Prepare announcement with upgrade instructions

---

## Activation Methods

### Method 1: Soft Fork (Preferred)

Activate at a future block height with sufficient notice:

```cpp
// In src/kernel/chainparams.cpp - Mainnet
consensus.nArgon2ForkHeight = ACTIVATION_HEIGHT;  // Set future block
consensus.fArgon2Emergency = true;
```

**Timeline:**
- Announce: T-0
- Node upgrade deadline: T+3 days
- Activation height: T+7 days minimum

### Method 2: Flag Day (Faster)

Activate at a specific Unix timestamp:

```cpp
// In src/kernel/chainparams.cpp
consensus.nArgon2ActivationTime = UNIX_TIMESTAMP;
consensus.fArgon2Emergency = true;
```

**Use when:** Block production is already compromised.

### Method 3: Manual Override (Emergency)

For operators who need immediate protection:

```bash
# Start node with forced Argon2
opensyd -forcepow=argon2 -argon2height=CURRENT_HEIGHT
```

⚠️ **Warning:** This forks you from the main chain until others upgrade.

---

## Code Changes Required

### 1. Update Consensus Parameters

File: `src/kernel/chainparams.cpp`

```cpp
// EMERGENCY ACTIVATION - Date: YYYY-MM-DD
// Reason: [Brief description of threat]
// Coordination: [Link to announcement]

// Mainnet
consensus.nArgon2ForkHeight = 500000;  // Example height
consensus.fArgon2Emergency = true;
consensus.nArgon2MemoryCost = 2097152;  // 2 GB (matches mainnet default / RandomX dataset)
consensus.nArgon2TimeCost = 1;          // Matches code default (consensus/params.h)
consensus.nArgon2Parallelism = 1;
```

### 2. Update Version

File: `src/clientversion.h`

```cpp
#define CLIENT_VERSION_MINOR 1  // Bump for emergency release
#define CLIENT_VERSION_BUILD 1  // Emergency build
```

### 3. Add Checkpoint

File: `src/kernel/chainparams.cpp`

```cpp
// Add checkpoint at last known-good block before issue
checkpointData = {
    {
        // ... existing checkpoints ...
        {LAST_GOOD_HEIGHT, uint256S("BLOCK_HASH")},
    }
};
```

---

## Argon2id Parameters

The emergency Argon2id configuration:

| Parameter | Value | Description |
|-----------|-------|-------------|
| Memory Cost | 2097152 KiB (2 GB) | RAM required per hash |
| Time Cost | 3 | Iterations |
| Parallelism | 1 | Single-threaded |
| Hash Length | 32 bytes | Output size |
| Salt | Block header hash | Unique per block |

### Tuning for Hardware

The default parameters are tuned for:
- Consumer CPUs (4+ cores)
- 8 GB RAM minimum
- ~2 second hash time

To verify on your hardware:

```bash
# Run Argon2 benchmark
./build/bin/test_opensy --run_test=argon2_benchmark
```

---

## Post-Activation Procedures

### 1. Monitor Network

```bash
# Watch block production
watch -n 10 'opensy-cli getblockchaininfo | jq ".blocks, .difficulty"'

# Check peer connectivity
opensy-cli getpeerinfo | jq '.[].subver'

# Verify Argon2 is active
opensy-cli getblocktemplate '{"rules":["segwit"]}' | jq '.pow_algorithm'
```

### 2. Communicate Status

- Update status page
- Post confirmation to channels
- Acknowledge successful transition

### 3. Plan Return to RandomX

Once RandomX is patched:

1. Test patched RandomX on testnet
2. Coordinate return activation height
3. Release with RandomX re-enabled
4. Monitor transition

---

## Rollback Procedure

If Argon2 activation was premature:

⚠️ **This is a hard fork and will cause chain split**

```bash
# Revert to pre-emergency version
opensyd --reindex -forcepow=randomx

# Or invalidate emergency blocks
opensy-cli invalidateblock FIRST_ARGON2_BLOCK_HASH
```

Only do this if:
- Activation was clearly in error
- Majority of network hasn't upgraded
- Within 24 hours of activation

---

## Contact & Escalation

### Emergency Contacts

| Role | Contact |
|------|---------|
| Lead Developer | [GitHub @opensyria] |
| Security Team | security@opensyria.net |
| Community Telegram | t.me/opensyria |
| Twitter/X | @OpenSY |

### Decision Authority

Emergency activation requires approval from:
- [ ] At least 2 core maintainers
- [ ] Major mining pool operators (if applicable)
- [ ] Community vote (if time permits)

---

## Appendix: Testing Emergency Activation

### On Regtest

```bash
# Start regtest with Argon2 at block 1
opensyd -regtest -argon2height=1

# Mine Argon2 blocks
opensy-cli -regtest generatetoaddress 10 $(opensy-cli -regtest getnewaddress)

# Verify algorithm
opensy-cli -regtest getblock $(opensy-cli -regtest getbestblockhash) | jq '.pow_algorithm'
```

### Functional Tests

```bash
# Run Argon2 transition tests
python3 test/functional/feature_argon2_fallback.py
python3 test/functional/feature_argon2_stress.py
```

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | Jan 2025 | OpenSY Team | Initial document |

---

*This document is part of OpenSY operational procedures. Store securely and review annually.*
