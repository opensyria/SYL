# SRC-20 Token Standard Specification

**Version:** 1.0  
**Status:** Final  
**Authors:** The OpenSY Developers  
**Created:** December 2024

---

## Abstract

SRC-20 is the native token standard for the OpenSY blockchain, enabling permissionless token issuance, transfer, and burn operations directly on the OpenSY Layer 1 network. This specification defines the protocol format, validation rules, and implementation requirements.

---

## Motivation

Syria needs a token standard that:

1. **Works with existing infrastructure** - No consensus changes required
2. **Is backwards compatible** - Old nodes still validate blocks
3. **Is light-client friendly** - Token operations visible without full state
4. **Is gas-free** - Only standard transaction fees apply
5. **Is simple** - Easy to implement in wallets and explorers

---

## Protocol Overview

SRC-20 tokens are embedded in standard OpenSY transactions using `OP_RETURN` outputs. The protocol uses a compact binary format to minimize on-chain footprint.

### Data Format

All SRC-20 operations use the following structure:

```
OP_RETURN <PROTOCOL_ID> <VERSION> <ACTION> <DATA>
```

| Field | Size | Description |
|-------|------|-------------|
| PROTOCOL_ID | 5 bytes | `0x53 0x52 0x43 0x32 0x30` ("SRC20") |
| VERSION | 1 byte | Protocol version (currently `0x01`) |
| ACTION | 1 byte | Operation code (see below) |
| DATA | Variable | Action-specific payload |

### Action Codes

| Code | Action | Description |
|------|--------|-------------|
| `0x01` | ISSUE | Create new token |
| `0x02` | TRANSFER | Transfer tokens |
| `0x03` | BURN | Destroy tokens permanently |
| `0x04` | FREEZE | Freeze an address (issuer only) — **reserved, not yet implemented** |
| `0x05` | UNFREEZE | Unfreeze an address (issuer only) — **reserved, not yet implemented** |

---

## Token Issuance (ISSUE)

### Payload Format

```
<TICKER:4> <NAME:32> <DECIMALS:1> <SUPPLY:8> <METADATA_HASH:32>
```

Total payload: **77 bytes** (fixed size).

| Field | Size | Constraints |
|-------|------|-------------|
| TICKER | 4 bytes | ASCII A-Z0-9, null-padded |
| NAME | 32 bytes | UTF-8, null-padded, 1-32 printable characters |
| DECIMALS | 1 byte | 0-18 |
| SUPPLY | 8 bytes | Little-endian uint64, > 0 |
| METADATA_HASH | 32 bytes | SHA256 of off-chain metadata (optional, zero-filled if none) |

> **Note:** There is no separate NAME_LEN field.  The NAME field is a fixed
> 32-byte slot; unused trailing bytes are null (`0x00`).  The parser strips
> trailing nulls to recover the variable-length string.

### Token ID Generation

The Token ID is the issuance transaction's txid, stored as a full 32-byte
uint256:

```
TokenID = txid  (32 bytes, the hash of the issuance transaction)
```

This ensures:
- Uniqueness across all tokens (each txid is globally unique)
- Simple verification (look up the issuance tx)
- No additional computation required

### Validation Rules

1. **Ticker**: 3-4 uppercase alphanumeric characters (`MIN_TICKER_LENGTH=3`, `MAX_TICKER_LENGTH=4`)
2. **Name**: 1-32 printable characters, no leading/trailing/consecutive spaces
3. **Decimals**: 0-18 (18 is maximum, matching Ethereum)
4. **Supply**: Must be > 0
5. **Uniqueness**: Ticker uniqueness is NOT enforced at consensus level (first-seen for verified status)
6. **Operations per tx**: A single transaction may carry at most **4** SRC-20 operations (`MAX_OPS_PER_TX = 4` in `src/script/src20.cpp`)
7. **Operations per block**: At most **100** token operations per block (`MAX_TOKENS_PER_BLOCK = 100`)

### Reserved Tickers

The following tickers are reserved and cannot be issued as SRC-20 tokens.
The canonical list lives in `src/script/src20.h` (`RESERVED_TICKERS`).

| Ticker | Purpose |
|--------|---------|
| `SYL` | Native currency |
| `OSYL` | OpenSYL variation |
| `ESYP` | Electronic Syrian Pound stablecoin |
| `SUSD` | Synthetic USD |
| `SUST` | Synthetic USDT |
| `BTC` | Prevent Bitcoin impersonation |
| `ETH` | Prevent Ethereum impersonation |
| `USDT` | Reserved for bridged Tether |
| `USDC` | Reserved for bridged USD Coin |

Additional tickers may be reserved at runtime via `AddReservedTicker()`.

### Example Transaction

```
Inputs:
  - UTXO from issuer wallet (for fees)

Outputs:
  [0] OP_RETURN SRC20 01 01 <issuance_data>
  [1] Issuer address (receives all tokens + dust)
  [2] Change address (remaining SYL)
```

---

## Token Transfer (TRANSFER)

### Payload Format

```
<TOKEN_ID:32> <AMOUNT:8>
```

| Field | Size | Constraints |
|-------|------|-------------|
| TOKEN_ID | 32 bytes | Valid existing token (issuance txid) |
| AMOUNT | 8 bytes | Little-endian uint64, > 0 |

### Recipient Determination

The recipient is determined from **output[1]** (the second output). The
transaction layout is:

- Output[0]: OP_RETURN with SRC-20 transfer data
- Output[1]: Recipient address (must be spendable)

Output[1] must:

1. Be a valid address script (P2PKH, P2SH, P2WPKH, P2WSH, P2TR)
2. Not be empty or unspendable (OP_RETURN)

### Validation Rules

1. Token must exist
2. Sender must have sufficient balance
3. Amount must be > 0
4. Recipient must be a valid address

### Example Transaction

```
Inputs:
  - UTXO from sender wallet

Outputs:
  [0] OP_RETURN SRC20 01 02 <transfer_data>
  [1] Recipient address (dust amount, e.g., 546 satoshis)
  [2] Change address
```

---

## Token Burn (BURN)

### Payload Format

```
<TOKEN_ID:32> <AMOUNT:8>
```

### Validation Rules

1. Token must exist
2. Sender must have sufficient balance
3. Amount must be > 0
4. Burned tokens are permanently destroyed (reducing circulating supply)

### Example Transaction

```
Inputs:
  - UTXO from burner wallet

Outputs:
  [0] OP_RETURN SRC20 01 03 <burn_data>
  [1] Change address
```

---

## Indexing Requirements

### Node Configuration

All OpenSY nodes automatically track token state - no special configuration is required. The token index is built-in and always enabled.

```bash
# Token indexing is automatic - just start the node
opensyd
```

### Database Schema

The token index maintains:

1. **Token Registry**: All issued tokens with metadata
2. **Balance Index**: Per-address token balances
3. **Transfer History**: All token movements

### Reorg Handling

On blockchain reorganization:
1. Token operations in disconnected blocks are reverted
2. Balances are recalculated from the new chain tip
3. Pending mempool operations are re-validated

---

## RPC Interface

### Token Information

```bash
# Get token details
opensy-cli gettokeninfo <token_id>

# List all tokens
opensy-cli listtokens [count] [skip]

# Get token by ticker (first match)
opensy-cli gettokenbyticker <ticker>
```

### Balance Queries

```bash
# Get address token balance
opensy-cli gettokenbalance <address> <token_id>

# List all token balances for address
opensy-cli getaddresstokens <address>
```

### Token Operations

```bash
# Issue new token
opensy-cli issuetoken <ticker> <name> <supply> <decimals> [metadata_hash]

# Transfer tokens
opensy-cli transfertoken <token_id> <to_address> <amount>

# Burn tokens
opensy-cli burntoken <token_id> <amount>
```

---

## Consensus Rules

### Block Token Limit

To prevent spam, each block can contain a maximum of **100 token operations**.

```cpp
static constexpr size_t MAX_TOKENS_PER_BLOCK = 100;
```

Blocks exceeding this limit are invalid.

### Fee Requirements

Token operations require standard transaction fees. No additional "gas" fees apply.

Recommended minimums:
- ISSUE: 0.001 SYL (higher for priority)
- TRANSFER: 0.0001 SYL
- BURN: 0.0001 SYL

---

## Security Considerations

### Issuer Privileges

The issuing address retains special privileges:
- FREEZE/UNFREEZE operations (if implemented)
- Verified status on explorers

**Recommendation**: Use hardware wallets or multisig for issuer keys.

### Spam Prevention

1. Block token limit (100 ops/block)
2. Standard transaction fees
3. Minimum output values (dust threshold)

### Replay Protection

Token operations are tied to specific UTXOs, preventing replay attacks across transactions.

---

## Implementation Notes

### Source Files

| File | Description |
|------|-------------|
| `src/script/src20.h` | Protocol constants and structures |
| `src/script/src20.cpp` | Script parsing and building |
| `src/tokens/tokenvalidation.cpp` | Validation logic |
| `src/tokens/tokendb.cpp` | Database operations |
| `src/rpc/tokens.cpp` | RPC interface |

### Testing

```bash
# Unit tests
./build/bin/test_opensy -t src20_tests
./build/bin/test_opensy -t token_validation_tests

# Functional tests
python3 test/functional/feature_src20_tokens.py
python3 test/functional/rpc_src20.py
```

---

## Appendix A: Error Codes

| Code | Name | Description |
|------|------|-------------|
| `OK` | Success | Operation valid |
| `INVALID_FORMAT` | Bad format | Malformed SRC-20 data |
| `INVALID_VERSION` | Bad version | Unsupported protocol version |
| `INVALID_ACTION` | Bad action | Unknown action code |
| `INVALID_TICKER` | Bad ticker | Ticker validation failed |
| `RESERVED_TICKER` | Reserved | Ticker is reserved |
| `DUPLICATE_TICKER` | Duplicate | Ticker already exists (verified) |
| `TOKEN_NOT_FOUND` | Not found | Token ID doesn't exist |
| `INSUFFICIENT_BALANCE` | No funds | Not enough tokens |
| `INVALID_AMOUNT` | Bad amount | Amount is zero or invalid |
| `MISSING_RECIPIENT` | No recipient | Transfer has no valid recipient |
| `BLOCK_TOKEN_LIMIT` | Limit exceeded | Too many token ops in block |

---

## Appendix B: Constants

```cpp
// Protocol
static constexpr std::string_view SRC20_PROTOCOL_ID = "SRC20";
static constexpr uint8_t SRC20_VERSION = 0x01;

// Limits
static constexpr size_t MAX_TICKER_LENGTH = 4;
static constexpr size_t MAX_NAME_LENGTH = 32;
static constexpr uint8_t MAX_DECIMALS = 18;
static constexpr size_t MAX_TOKENS_PER_BLOCK = 100;

// Token ID
static constexpr size_t TOKEN_ID_SIZE = 32;  // 256 bits (full txid)
```

---

## Appendix C: Example Implementations

### Python (Wallet SDK)

```python
from opensy import Wallet, TokenBuilder

wallet = Wallet.from_mnemonic("your mnemonic words...")

# Issue token
token = TokenBuilder() \
    .ticker("DEMO") \
    .name("Demo Token") \
    .decimals(8) \
    .supply(1_000_000_000_00000000) \
    .build()

txid = wallet.issue_token(token)
print(f"Token issued: {txid}")

# Transfer
wallet.transfer_token(
    token_id="abc123...",
    to="syl1recipient...",
    amount=100_00000000
)
```

### JavaScript (Browser)

```javascript
import { OpenSYWallet } from '@opensy/wallet-sdk';

const wallet = await OpenSYWallet.create({ mnemonic: '...' });

// Issue token
const tokenId = await wallet.issueToken({
  ticker: 'DEMO',
  name: 'Demo Token',
  decimals: 8,
  totalSupply: '1000000000', // Human-readable
});

// Transfer
await wallet.transferToken({
  tokenId,
  to: 'syl1recipient...',
  amount: '100', // Human-readable
});
```

---

## ⚠️ Non-Consensus Advisory

> **IMPORTANT:** SRC-20 tokens are currently a **non-consensus overlay**. Token state
> is indexed locally by each node and is NOT enforced by miners or validated during
> block acceptance. This means:
>
> - Two nodes running different software versions may show different token balances
> - A token "transfer" is only recognized by nodes that index SRC-20 operations
> - Token failures do not cause block rejection — the base-layer SYL chain is unaffected
> - Token balances should be treated as **advisory** for high-value settlement
>
> A future hard-fork upgrade (SIP-TBD) will commit a Merkle root of token state
> into the coinbase transaction, making token balances consensus-enforced. Until
> then, SRC-20 tokens are suitable for community tokens, loyalty points, and
> low-value use cases — but should not be relied upon for critical financial settlement.

---

## Roadmap: Token Consensus Commitment

The following roadmap outlines the path from the current overlay model to full
consensus-enforced tokens.

### Phase 1: Overlay (Current — v1.0)
- Token state stored in per-node LevelDB (`<datadir>/tokens/`)
- OP_RETURN-based protocol — no consensus changes
- Atomic connect/disconnect in `ConnectBlock`/`DisconnectTip`
- 12 RPCs (node) + 5 RPCs (wallet) with advisory warnings
- **Status:** Shipped, dormant (0 tokens issued on mainnet as of Feb 2026)

### Phase 2: Deterministic Validation (Target: v1.1)
- Pin exact token validation rules so all nodes produce identical state
- Add `gettokenstateroot` RPC that computes Merkle root of current token state
- Add functional tests that verify state root consistency across reorgs
- Publish SIP (SYL Improvement Proposal) for consensus commitment format
- **Effort:** ~2-4 weeks of development

### Phase 3: Soft-Fork Commitment (Target: v2.0)
- Miners commit token state Merkle root in coinbase `OP_RETURN`
- Old nodes ignore the commitment (backwards compatible)
- New nodes validate the commitment — reject blocks with wrong token root
- BIP9-style activation with miner signalling (75% threshold)
- **Effort:** ~2-3 months of development + testnet soak period

### Phase 4: Full Consensus Enforcement (Target: v3.0)
- Token state divergence becomes a consensus failure (chain split)
- SPV proofs for token balances (light clients can verify)
- Token-aware fee estimation and mempool policy
- **Effort:** ~3-6 months, requires ecosystem readiness

### Design Decisions (for SIP authors)

1. **Commitment location:** Coinbase `OP_RETURN` output (index 1), 32-byte Merkle root
2. **Merkle tree structure:** Sorted by `(token_id, address)` key, SHA256d internal nodes
3. **Activation mechanism:** BIP9 with `bit=3`, 75% threshold over 10,080-block period
4. **Migration:** All existing tokens grandfathered — no re-issuance needed
5. **Rollback safety:** Token undo records already stored per-block; consensus commitment
   adds a checksum but doesn't change the disconnect logic

---

## Changelog

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | Dec 2024 | Initial specification |
| 1.0.1 | Feb 2026 | Added non-consensus advisory, roadmap, and design decisions |

---

*For questions or proposals, open a GitHub issue or join [t.me/OpenSYDev](https://t.me/OpenSYDev)*
