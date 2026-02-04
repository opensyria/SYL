# OpenSY Developer Documentation

This directory contains technical documentation for OpenSY developers and contributors.

## Core Documentation

| Document | Description |
|----------|-------------|
| [src20-spec.md](src20-spec.md) | SRC-20 Token Standard Specification |
| [token-indexing.md](token-indexing.md) | Token Indexing & Scalability Guide |
| [emergency-pow-activation.md](emergency-pow-activation.md) | Emergency PoW Activation Runbook |
| [checkpoints.md](checkpoints.md) | Checkpoint Policy & Procedures |
| [upgrade-guide.md](upgrade-guide.md) | Node Upgrade & Migration Guide |
| [upstream-sync.md](upstream-sync.md) | Tracking Bitcoin Core cherry-picks |

## Architecture Overview

OpenSY is forked from Bitcoin Core with the following key modifications:

### Proof-of-Work System

OpenSY uses a **two-phase PoW system**:

1. **Phase 1: SHA256d** (Blocks 0 - 209,999)
   - Standard Bitcoin double-SHA256
   - Chain bootstrapping phase (10% of total supply)
   - Proven algorithm for initial chain establishment

2. **Phase 2: RandomX** (Blocks 210,000+)
   - CPU-optimized, ASIC-resistant algorithm
   - 90% of total supply for community mining
   - Key rotation every 32 blocks

3. **Emergency Fallback: Argon2id** (Dormant)
   - Activated only if RandomX is compromised
   - Memory-hard alternative

See: `src/pow.cpp`, `src/consensus/params.h`

### SRC-20 Token Standard

OpenSY supports native token issuance via the SRC-20 protocol:

- **Issue**: Create new tokens with ticker, supply, decimals
- **Transfer**: Send tokens between addresses
- **Burn**: Permanently destroy tokens

See: `src/script/src20.h`, `src/tokens/`

### Consensus Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| Block Time | 2 minutes | Target time between blocks |
| Difficulty Adjustment | 10,080 blocks | ~2 weeks adjustment period |
| Block Reward | 10,000 SYL | Coinbase reward |
| Halving Interval | 1,050,000 blocks | ~4 years |
| Total Supply | 21,000,000,000 SYL | Maximum coins |

### Network

| Network | Port | Address Prefix |
|---------|------|----------------|
| Mainnet | 9633 | F (syl1...) |
| Testnet | 19633 | f (tsyl1...) |
| Regtest | 19000 | f |

## Building

See the main [README.md](../README.md) for build instructions.

### Build Options

```bash
# Debug build
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Release build
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Without wallet
cmake -B build -DENABLE_WALLET=OFF

# Without GUI
cmake -B build -DWITH_GUI=OFF
```

## Testing

```bash
# All unit tests
./build/bin/test_opensy

# Specific test
./build/bin/test_opensy --run_test=pow_tests

# Functional tests
cd test/functional
./test_runner.py
```

## RPC API

OpenSY supports all Bitcoin Core RPC commands plus:

### Token RPCs
- `createtoken` - Issue a new SRC-20 token
- `transfertoken` - Transfer tokens to an address
- `burntoken` - Burn tokens
- `gettokenbalance` - Get token balance
- `listtokens` - List all tokens
- `gettokenhistory` - Get token transaction history

## Further Reading

- [Bitcoin Core Developer Notes](https://github.com/bitcoin/bitcoin/blob/master/doc/developer-notes.md)
- [RandomX Specification](https://github.com/tevador/RandomX/blob/master/doc/specs.md)
- [Argon2 RFC 9106](https://datatracker.ietf.org/doc/rfc9106/)

## Contributing

See [CONTRIBUTING.md](../CONTRIBUTING.md) for contribution guidelines.
