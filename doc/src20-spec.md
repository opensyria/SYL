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
| `0x04` | FREEZE | Freeze an address (issuer only) |
| `0x05` | UNFREEZE | Unfreeze an address (issuer only) |

---

## Token Issuance (ISSUE)

### Payload Format

```
<TICKER:4> <NAME_LEN:1> <NAME:1-32> <DECIMALS:1> <SUPPLY:8> <METADATA_HASH:32>
```

| Field | Size | Constraints |
|-------|------|-------------|
| TICKER | 4 bytes | ASCII A-Z0-9, null-padded |
| NAME_LEN | 1 byte | 1-32 |
| NAME | 1-32 bytes | UTF-8, printable characters |
| DECIMALS | 1 byte | 0-18 |
| SUPPLY | 8 bytes | Little-endian uint64, > 0 |
| METADATA_HASH | 32 bytes | SHA256 of off-chain metadata (optional, zero-filled if none) |

### Token ID Generation

The Token ID is deterministically generated as:

```
TokenID = SHA256(issuer_scriptPubKey || txid || vout)[:16]
```

This ensures:
- Uniqueness across all tokens
- Issuer verifiability
- No pre-computation attacks

### Validation Rules

1. **Ticker**: 1-4 uppercase alphanumeric characters
2. **Name**: 1-32 printable characters, no leading/trailing/consecutive spaces
3. **Decimals**: 0-18 (18 is maximum, matching Ethereum)
4. **Supply**: Must be > 0
5. **Uniqueness**: Ticker uniqueness is NOT enforced at consensus level (first-seen for verified status)

### Reserved Tickers

The following tickers are reserved:

| Ticker | Purpose |
|--------|---------|
| `SYL` | Native currency (cannot be issued as token) |
| `ESYP` | Reserved for Electronic Syrian Pound stablecoin |
| `USDT` | Reserved for bridged USDT |
| `USDC` | Reserved for bridged USDC |

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
<TOKEN_ID:16> <AMOUNT:8>
```

| Field | Size | Constraints |
|-------|------|-------------|
| TOKEN_ID | 16 bytes | Valid existing token |
| AMOUNT | 8 bytes | Little-endian uint64, > 0 |

### Recipient Determination

The recipient is determined by the **first non-OP_RETURN output** in the transaction. This output must:

1. Be a valid address script (P2PKH, P2SH, P2WPKH, P2WSH, P2TR)
2. Have at least dust value (546 satoshis)

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
  [0] Recipient address (dust amount, e.g., 546 satoshis)
  [1] OP_RETURN SRC20 01 02 <transfer_data>
  [2] Change address
```

---

## Token Burn (BURN)

### Payload Format

```
<TOKEN_ID:16> <AMOUNT:8>
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
static constexpr size_t TOKEN_ID_SIZE = 16;  // 128 bits
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

## Changelog

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | Dec 2024 | Initial specification |

---

*For questions or proposals, open a GitHub issue or join [t.me/OpenSYDev](https://t.me/OpenSYDev)*
