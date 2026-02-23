# OpenSY Mining Pool Integration Guide

This guide explains how to integrate OpenSY mining into a mining pool using the RandomX proof-of-work algorithm.

## Overview

OpenSY uses RandomX for proof-of-work (from block 210,000 onwards), making it CPU-mineable and ASIC-resistant. This guide covers pool integration for RandomX mining.

## Key Differences from Bitcoin

| Feature | Bitcoin | OpenSY |
|---------|---------|--------|
| PoW Algorithm | SHA256d | RandomX (from block 210,000) |
| Block Time | 10 minutes | 2 minutes |
| Block Reward | 6.25 BTC (current) | 10,000 SYL |
| Memory Required | Minimal | 2.5 GB (full dataset) |
| Stratum Protocol | Standard | Extended for RandomX |

## Pool Software Compatibility

### Recommended Pool Software

1. **Modified stratum-mining-pool** (RandomX fork)
2. **Custom implementation** using OpenSY RPC

### Required Modifications

Standard Bitcoin pool software needs modifications:
- RandomX hash validation instead of SHA256d
- Key block tracking for RandomX key rotation
- Memory management for RandomX contexts

## RPC Methods for Pools

### getblocktemplate

```bash
opensy-cli getblocktemplate '{"rules": ["segwit"]}'
```

Returns block template with proper RandomX parameters.

### submitblock

```bash
opensy-cli submitblock "hexdata"
```

Submits a solved block to the network.

### getrandomxpoolinfo

```bash
opensy-cli getrandomxpoolinfo
```

Returns RandomX context pool status:
```json
{
  "pool_size": 8,
  "available": 6,
  "in_use": 2,
  "total_acquisitions": 15432,
  "total_timeouts": 0,
  "starvation_events": 0,
  "current_key_hash": "000000..."
}
```

### getblockchaininfo

Check current block height and RandomX activation:
```bash
opensy-cli getblockchaininfo
```

Key fields:
- `blocks`: Current block height
- `chain`: Network (main, test, regtest)

## RandomX Key Management

### Key Rotation

RandomX uses a key derived from a previous block hash. The key changes every 32 blocks (mainnet).

```python
def get_key_block_height(current_height, interval=32):
    return (current_height // interval) * interval - interval
```

### Key Block Hash

Get the key block hash for mining:

```bash
# Get key block height
KEY_HEIGHT=$(($(opensy-cli getblockcount) / 32 * 32 - 32))

# Get key block hash
opensy-cli getblockhash $KEY_HEIGHT
```

## Stratum Protocol Extensions

### Job Parameters

When sending jobs to miners, include:
- Standard block template data
- RandomX key block hash
- Key block height (for validation)

### Share Validation

1. Compute RandomX hash using provided key
2. Compare hash to share difficulty target
3. If valid share, credit worker
4. If meets block difficulty, submit block

## Memory Considerations

### Server Requirements

| Miners | RAM Required | Notes |
|--------|--------------|-------|
| 1-10 | 4 GB | Single context |
| 10-100 | 8 GB | Context pool |
| 100+ | 16+ GB | Multiple pools |

### Context Pooling

OpenSY uses a context pool (max 8 contexts by default). For high-load pools, consider:

```bash
# Increase context pool (compile-time option)
cmake -DRANDOMX_MAX_CONTEXTS=16 ...
```

## Example Pool Flow

```
1. Pool: getblocktemplate → Get block template
2. Pool: Calculate RandomX key from current height
3. Pool: Send job to miners (template + key)
4. Miner: Compute RandomX hash
5. Miner: Submit share if meets difficulty
6. Pool: Validate share with RandomX
7. Pool: If block found, submitblock
8. Pool: Credit worker shares
```

## Payout Considerations

### Transaction Fees

- Token issuance requires a **100 SYL** fee (`MIN_TOKEN_ISSUANCE_FEE`); transfers and burns use standard fees
- Batch payouts to minimize fees
- Consider SYL-denominated payouts

### Minimum Payout

Recommended minimum: 100 SYL (to cover transaction fees)

## Monitoring

### Pool Health Checks

```bash
# Check node sync status
opensy-cli getblockchaininfo | jq '.verificationprogress'

# Check peer connections
opensy-cli getconnectioncount

# Check RandomX pool health
opensy-cli getrandomxpoolinfo
```

### Alert Thresholds

| Metric | Warning | Critical |
|--------|---------|----------|
| Block height lag | >2 blocks | >5 blocks |
| Peer count | <4 | <2 |
| RandomX timeouts | >0 | >10 |

## Testing

### Regtest Setup

```bash
# Start regtest node
opensyd -regtest -rpcuser=pool -rpcpassword=test

# Generate blocks for testing
opensy-cli -regtest generatetoaddress 110 $(opensy-cli -regtest getnewaddress)
```

### Testnet

Use testnet for integration testing:

```bash
opensyd -testnet
```

## Security

### RPC Security

```ini
# opensy.conf
rpcuser=pool_user
rpcpassword=<strong_random_password>
rpcallowip=127.0.0.1
rpcbind=127.0.0.1
```

### DDoS Protection

- Rate limit worker connections
- Validate share difficulty before processing
- Use connection pooling

## Support

- **GitHub**: https://github.com/opensyria/SYL/issues
- **Documentation**: https://docs.opensyria.net
- **Pool Operators Channel**: Contact maintainers for access

---

*Pool operators who successfully integrate OpenSY may be listed on the official website.*
