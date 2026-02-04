# OpenSY Upgrade Guide

**Document:** Node Upgrade & Migration Procedures  
**Version:** 1.0  
**Last Updated:** January 2025

---

## Overview

This guide covers upgrading OpenSY nodes between versions, including:
- Standard version upgrades
- Major version migrations
- Network fork transitions
- Data migration procedures

---

## Quick Upgrade (Minor Versions)

For minor version upgrades (e.g., 1.0.0 → 1.0.1):

```bash
# 1. Stop the node
opensy-cli stop

# 2. Backup wallet (always!)
cp -r ~/.opensy/wallets ~/opensy-wallet-backup-$(date +%Y%m%d)

# 3. Update binary
cd /path/to/SYL
git pull
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# 4. Restart
./build/bin/opensyd -daemon

# 5. Verify
./build/bin/opensy-cli getblockchaininfo
```

---

## Major Version Upgrades

For major upgrades with database changes:

### Before Upgrade

```bash
# Check current version
opensy-cli --version

# Check blockchain state
opensy-cli getblockchaininfo

# Backup everything
cp -r ~/.opensy ~/opensy-backup-$(date +%Y%m%d)
```

### During Upgrade

```bash
# Stop node
opensy-cli stop

# Wait for clean shutdown
sleep 10

# Build new version
cd /path/to/SYL
git fetch origin
git checkout v2.0.0  # Example new version
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### After Upgrade

```bash
# Start with reindex if required by release notes
./build/bin/opensyd -reindex

# Monitor progress
tail -f ~/.opensy/debug.log

# Verify sync
./build/bin/opensy-cli getblockchaininfo
```

---

## Network Fork Transitions

### RandomX Activation (Block 210,000)

When the network transitions from SHA256d to RandomX:

**No action required** - The transition is automatic at block 210,000.

To verify your node is ready:

```bash
# Check that RandomX is compiled in
opensy-cli getmininginfo

# Should show "pow_algorithm" field
```

### Emergency PoW Activation

If Argon2id emergency activation is announced:

1. Monitor official channels for activation block
2. Upgrade to specified version before activation
3. See [emergency-pow-activation.md](emergency-pow-activation.md)

---

## Data Directory Migration

### Moving Data to New Location

```bash
# Stop node
opensy-cli stop

# Move data
mv ~/.opensy /new/path/opensy-data

# Start with new location
opensyd -datadir=/new/path/opensy-data
```

Or set permanently in config:

```ini
# ~/.opensy/opensy.conf (or new location)
datadir=/new/path/opensy-data
```

### Migrating to New Machine

```bash
# On old machine
opensy-cli stop
tar -czf opensy-data.tar.gz ~/.opensy

# Transfer to new machine
scp opensy-data.tar.gz newmachine:/tmp/

# On new machine
cd ~
tar -xzf /tmp/opensy-data.tar.gz
./opensyd -daemon
```

**Note:** Wallet files are encrypted. You'll need your wallet passphrase.

---

## Wallet Migration

### Exporting Wallet

```bash
# Backup wallet file
cp ~/.opensy/wallets/mywallet/wallet.dat ~/wallet-backup.dat

# Or export descriptors (HD wallets)
opensy-cli -rpcwallet=mywallet listdescriptors true > wallet-descriptors.json
```

### Importing to New Node

```bash
# From wallet.dat
mkdir -p ~/.opensy/wallets/mywallet
cp ~/wallet-backup.dat ~/.opensy/wallets/mywallet/wallet.dat

# From descriptors (create new wallet first)
opensy-cli createwallet "restored" true true "" false true
opensy-cli -rpcwallet=restored importdescriptors "$(cat wallet-descriptors.json)"
```

### Migrating Legacy to Descriptor Wallet

For wallets created before descriptor support:

```bash
# Check wallet type
opensy-cli -rpcwallet=oldwallet getwalletinfo

# Migrate (creates new descriptor wallet)
opensy-cli -rpcwallet=oldwallet migratewallet
```

---

## Token Index Migration

Token indexing is now built-in and always enabled. If upgrading from an older version, simply restart your node - the token database will be created automatically.

If you need to rebuild the token index:

```bash
# Reindex to rebuild token database
opensyd -reindex
```

**Expected time:** ~1 hour per 100,000 blocks

---

## Troubleshooting Upgrades

### "Database corruption detected"

```bash
# Stop and rebuild indexes
opensy-cli stop
opensyd -reindex
```

### "Incompatible database version"

```bash
# Full reindex from blocks
opensy-cli stop
rm -rf ~/.opensy/chainstate
rm -rf ~/.opensy/blocks/index
opensyd -reindex
```

### "Unknown block version"

You may be on a fork. Ensure you're on the correct chain:

```bash
# Check best block
opensy-cli getbestblockhash

# Compare with explorer
# If different, you may need to invalidate blocks
opensy-cli invalidateblock <wrong_block_hash>
```

### Wallet won't load after upgrade

```bash
# Try loading with salvage
opensy-cli loadwallet "mywallet" true  # load_on_startup=true

# Or use salvagewallet (destructive)
opensyd -salvagewallet
```

---

## Downgrade Procedures

**⚠️ Warning:** Downgrades are not always possible. Check release notes.

### If Downgrade is Safe

```bash
opensy-cli stop
git checkout v1.0.0  # Previous version
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/bin/opensyd -daemon
```

### If Database Format Changed

```bash
# Restore from backup
opensy-cli stop
rm -rf ~/.opensy
tar -xzf opensy-backup-YYYYMMDD.tar.gz -C ~/
opensyd -daemon
```

---

## Pre-Upgrade Checklist

- [ ] Read release notes for breaking changes
- [ ] Backup wallet files
- [ ] Backup `opensy.conf`
- [ ] Note current block height
- [ ] Check disk space (reindex may need 2x space temporarily)
- [ ] Schedule upgrade during low-activity period
- [ ] Have rollback plan ready

---

## Post-Upgrade Verification

```bash
# Check version
opensy-cli --version

# Check sync status
opensy-cli getblockchaininfo | jq '.blocks, .headers, .verificationprogress'

# Check peers
opensy-cli getpeerinfo | jq '.[].subver'

# Check wallet
opensy-cli getwalletinfo

# Check token index (if enabled)
opensy-cli listtokens
```

---

## Version History

| Version | Date | Notes |
|---------|------|-------|
| 1.0.0 | Dec 2024 | Initial mainnet release |

---

## Support

For upgrade issues:
- GitHub Issues: github.com/opensyria/SYL/issues
- Telegram: t.me/opensyria
- Email: support@opensyria.net

---

*Always backup before upgrading. When in doubt, ask in community channels.*
