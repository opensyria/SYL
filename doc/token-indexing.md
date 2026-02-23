# Token Indexing & Scalability Guide

**Document:** OpenSY Token System Operations  
**Version:** 1.1  
**Last Updated:** February 2026

---

## Overview

OpenSY's SRC-20 token system includes a built-in index for tracking token balances and history. Token indexing is **always enabled** - no configuration required. This document covers performance tuning and scalability considerations.

---

## Token Index (Built-in)

### Automatic Initialization

The token database is automatically initialized when the node starts. No configuration is needed - token RPCs work out of the box.

```bash
# Just start the node - token index is automatic
opensyd
```

### Rebuilding the Index

If you need to rebuild the token database (e.g., after data corruption):

```bash
# Reindex to rebuild token database
opensyd -reindex
```

**Expected time:** ~1 hour per 100,000 blocks (depends on token activity)

---

## Storage Requirements

### Database Size Estimates

| Tokens | Addresses | Transfers | Est. DB Size |
|--------|-----------|-----------|--------------|
| 100 | 1,000 | 10,000 | ~10 MB |
| 1,000 | 10,000 | 100,000 | ~100 MB |
| 10,000 | 100,000 | 1,000,000 | ~1 GB |
| 100,000 | 1,000,000 | 10,000,000 | ~10 GB |

### Database Location

```
~/.opensy/
├── blocks/          # Block data
├── chainstate/      # UTXO set
└── tokens/          # Token index (always enabled, single LevelDB)
```

All token data (metadata, balances, history, undo records) is stored in a **single LevelDB instance** at `<datadir>/tokens/`, differentiated by key prefixes:

| Prefix | Byte | Purpose |
|--------|------|---------|
| `T` | Token registry | Token metadata by ID |
| `t` | Ticker index | Token ID lookup by ticker |
| `B` | Balance | Per-address token balances |
| `A` | Address tokens | Set of token IDs per address |
| `H` | History | Transfer history records |
| `X` | Stats | Aggregate token statistics |
| `U` | Undo | Block-level undo data for reorgs |
| `b` | Best block | Last processed block hash |
| `Z` | DB version | Schema version marker |

---

## Performance Tuning

### Memory Settings

For high-volume token activity:

```ini
# Increase database cache (default: 450 MB)
dbcache=1024

# Token-specific cache (if available in future versions)
# tokencache=256
```

### Block Processing

Token operations are validated during block connection:

| Operation | CPU Cost | I/O Cost |
|-----------|----------|----------|
| ISSUE | Low | Low |
| TRANSFER | Medium | Medium |
| BURN | Low | Low |

### Optimization Tips

1. **SSD Required** - Token DB uses random reads
2. **Increase dbcache** - Reduces disk I/O
3. **Prune with caution** - Token index requires full blocks for history

---

## Scalability Limits

### Consensus Limits

| Limit | Value | Rationale |
|-------|-------|-----------|
| Max tokens/block | 100 | Prevent spam |
| Max ops/tx | 4 | Prevent payload spam |
| Min ticker length | 3 chars | Prevent squatting |
| Max ticker length | 4 chars | Compact storage |
| Max name length | 32 chars | Reasonable display |
| Max decimals | 18 | Matches Ethereum |
| Max supply | 2^63-1 (INT64_MAX) | Safe signed arithmetic |
| Min issuance fee | 100 SYL | Prevent token spam |

### Practical Limits

| Metric | Tested Capacity | Notes |
|--------|----------------|-------|
| Total tokens | 1,000,000+ | Tested in simulation |
| Balances/address | 10,000+ | No hard limit |
| History depth | Unlimited | Prunable in future |
| Query speed | <100ms | For single address |

---

## High-Volume Scenarios

### Exchange Integration

For exchanges tracking many addresses:

```ini
# Increase connection limits
maxconnections=256

# Increase memory
dbcache=4096

# Enable bloom filters for address monitoring
peerbloomfilters=1
```

### Block Explorer Backend

For running an explorer:

```ini
# Full indexing
txindex=1

# Token indexing is automatic - no flag needed

# Increase RPC limits
rpcworkqueue=64
rpcthreads=8

# Address indexing (if available)
# addressindex=1
```

---

## Monitoring

### Check Token Index Status

```bash
# Token database size
du -sh ~/.opensy/tokens/

# Token count
opensy-cli listtokens 0 | jq '.total'

# Recent token activity
opensy-cli listtokens 10 0 | jq '.[].ticker'
```

### Performance Metrics

```bash
# Block processing time (includes token validation)
opensy-cli getblockchaininfo | jq '.verificationprogress'

# Memory usage
opensy-cli getmemoryinfo | jq '.locked'
```

---

## Known Limitations

### Current Version

1. **No pruning** - Token history grows indefinitely
2. **Single-threaded validation** - Token ops processed sequentially
3. **No SPV token proofs** - Full node required for token state

### Future Improvements (Roadmap)

- [x] Address-based token queries (implemented via `ADDR_TOKENS` prefix)
- [ ] Token history pruning
- [ ] Parallel token validation
- [ ] Light client token proofs
- [ ] Token state snapshots

---

## Troubleshooting

### Token Index Corruption

Symptoms:
- RPC errors: "Token database error"
- Missing balances
- Incorrect history

Fix:

```bash
# Stop node
opensy-cli stop

# Remove token database
rm -rf ~/.opensy/tokens/

# Reindex to rebuild
opensyd -reindex
```

### Slow Token Queries

Symptoms:
- RPCs timeout
- High CPU during queries

Fix:

```ini
# Increase cache
dbcache=2048

# Or disable history queries if not needed
# (future option)
# tokenhistory=0
```

### High Memory Usage

Symptoms:
- OOM killer
- Swap thrashing

Fix:

```ini
# Reduce cache
dbcache=256

# Limit connections
maxconnections=32
```

---

## Benchmarks

### Query Performance (Reference Hardware)

Hardware: 8-core CPU, 32GB RAM, NVMe SSD

| Query | Tokens | Addresses | Time |
|-------|--------|-----------|------|
| gettokeninfo | 10,000 | - | <1ms |
| gettokenbalance | 10,000 | 100,000 | <5ms |
| listtokens (100) | 10,000 | - | <10ms |
| gettokenholders | 10,000 | 100,000 | <50ms |

### Block Validation Time

| Token ops/block | Validation overhead |
|-----------------|---------------------|
| 0 | 0ms |
| 10 | <5ms |
| 50 | <20ms |
| 100 (max) | <50ms |

---

## API Rate Limiting

For public-facing nodes, consider:

```ini
# Limit RPC connections
rpcallowip=127.0.0.1

# Use reverse proxy with rate limiting
# (nginx, caddy, etc.)
```

Example nginx rate limit:

```nginx
limit_req_zone $binary_remote_addr zone=rpc:10m rate=10r/s;

location /rpc {
    limit_req zone=rpc burst=20;
    proxy_pass http://127.0.0.1:9634;
}
```

---

## Disaster Recovery

### Backup Token Index

```bash
# Stop node first
opensy-cli stop

# Backup token database
tar -czf tokens-backup-$(date +%Y%m%d).tar.gz ~/.opensy/tokens/

# Restart
opensyd -daemon
```

### Restore from Backup

```bash
# Stop node
opensy-cli stop

# Restore
tar -xzf tokens-backup-YYYYMMDD.tar.gz -C ~/

# Start (will validate)
opensyd -daemon
```

### Full Rebuild

If backup unavailable:

```bash
# Reindex from blocks (slowest but always works)
opensyd -reindex
```

---

## Appendix: Configuration Reference

| Option | Default | Description |
|--------|---------|-------------|
| `-dbcache` | 450 | Database cache in MB (increase for better token performance) |
| `-reindex` | 0 | Rebuild all indexes including token database |
| `-checkblocks` | 6 | Blocks to check at startup |

> **Note:** Token indexing is always enabled. There is no `-tokenindex` flag - it's a built-in feature.

---

*For support, open an issue at github.com/opensyria/SYL or join t.me/opensyria*
