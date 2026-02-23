// Copyright (c) 2024-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <script/src20.h>

#include <hash.h>
#include <logging.h>
#include <streams.h>
#include <sync.h>
#include <tinyformat.h>

#include <algorithm>
#include <limits>
#include <set>

namespace src20 {

std::string ActionToString(TokenAction action)
{
    switch (action) {
        case TokenAction::ISSUE: return "ISSUE";
        case TokenAction::TRANSFER: return "TRANSFER";
        case TokenAction::BURN: return "BURN";
        case TokenAction::INVALID: return "INVALID";
    }
    return "UNKNOWN";
}

TokenAction ActionFromByte(uint8_t byte)
{
    switch (byte) {
        case 0x01: return TokenAction::ISSUE;
        case 0x02: return TokenAction::TRANSFER;
        case 0x03: return TokenAction::BURN;
        default: return TokenAction::INVALID;
    }
}

std::optional<TokenId> TokenId::FromHex(const std::string& hex)
{
    auto hash = uint256::FromHex(hex);
    if (!hash) {
        return std::nullopt;
    }
    return TokenId(*hash);
}

bool TokenIssuance::IsValid() const
{
    // Ticker must be 3-4 characters, ASCII alphanumeric uppercase ONLY
    // AUDIT FIX [M-06]: Enforce MIN_TICKER_LENGTH=3 to prevent single-char
    // ticker squatting that could exhaust the namespace.
    // SECURITY: We explicitly reject any non-ASCII bytes to prevent Unicode
    // confusable attacks (e.g., Cyrillic "А" vs Latin "A", Greek "Ο" vs "O")
    if (ticker.size() < MIN_TICKER_LENGTH || ticker.size() > MAX_TICKER_LENGTH) {
        return false;
    }
    for (unsigned char c : ticker) {
        // Only allow ASCII uppercase letters and digits
        // Any byte >= 0x80 is rejected (multi-byte UTF-8)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
            return false;
        }
    }

    // Name must not be empty and max 32 characters
    if (name.empty() || name.size() > MAX_NAME_LENGTH) {
        return false;
    }

    // AUDIT FIX [R21-01]: Validate name characters at the IsValid() level
    // (defense-in-depth).  Previously only the mempool validator
    // (ValidateIssuance) enforced character restrictions, so a miner could
    // insert XSS-prone or control-character names via block processing which
    // only calls IsValid().  Allowed: A-Z a-z 0-9 space hyphen period parens.
    for (size_t i = 0; i < name.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(name[i]);
        // Reject control characters (0x00-0x1F, 0x7F) and non-ASCII (>= 0x80)
        if (c < 0x20 || c == 0x7F || c >= 0x80) {
            return false;
        }
        bool valid =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == ' ' || c == '-' || c == '.' ||
            c == '(' || c == ')';
        if (!valid) {
            return false;
        }
    }

    // Reject names that are only whitespace, have leading/trailing spaces,
    // or contain consecutive spaces (can confuse users/UIs).
    if (name.front() == ' ' || name.back() == ' ') {
        return false;
    }
    if (name.find("  ") != std::string::npos) {
        return false;
    }

    // Decimals must be 0-18
    if (decimals > MAX_DECIMALS) {
        return false;
    }

    // Supply must be non-zero
    if (total_supply == 0) {
        return false;
    }

    // AUDIT FIX [R21-01]: Enforce INT64_MAX supply cap for ALL decimal values.
    // The mempool overlay (MempoolTokenState) uses int64_t deltas internally,
    // so supplies above INT64_MAX cause silent overflow in pending-balance
    // tracking.  Previously only decimals>0 had a cap; decimals==0 allowed
    // up to UINT64_MAX, which a miner could exploit to corrupt mempool state.
    if (total_supply > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return false;
    }

    // SECURITY FIX [L-02]: Validate maximum supply to prevent overflow in display calculations
    // When supply * 10^decimals would exceed UINT64_MAX, wallet software could overflow
    // when converting to human-readable format. We reject such tokens at issuance.
    // Example: supply=UINT64_MAX with decimals=18 would overflow any display calculation
    if (decimals > 0) {
        // Calculate the maximum safe supply for this decimal count
        // max_safe = UINT64_MAX / 10^decimals (prevents display overflow)
        uint64_t divisor = 1;
        for (uint8_t d = 0; d < decimals; ++d) {
            divisor *= 10;
        }
        uint64_t max_safe_supply = UINT64_MAX / divisor;
        if (total_supply > max_safe_supply) {
            return false;
        }
    }

    // Check for reserved tickers
    if (reserved::IsReservedTicker(ticker)) {
        return false;
    }

    return true;
}

std::string TokenIssuance::ToString() const
{
    return strprintf("TokenIssuance{ticker=%s, name=%s, decimals=%u, supply=%llu, metadata=%s}",
                     ticker, name, decimals, total_supply, metadata_hash.GetHex().substr(0, 16));
}

bool TokenTransfer::IsValid() const
{
    // AUDIT FIX [R28-02]: Cap amount at INT64_MAX to match the issuance
    // supply limit.  The mempool overlay uses int64_t deltas internally,
    // so amounts above INT64_MAX would silently overflow pending-balance
    // tracking.  Downstream validators (tokenvalidation.cpp) also check
    // this, but defense-in-depth demands the canonical IsValid() gate
    // rejects them.
    return !token_id.IsNull() && amount > 0 &&
           amount <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
}

std::string TokenTransfer::ToString() const
{
    return strprintf("TokenTransfer{token=%s, amount=%llu}",
                     token_id.ToString().substr(0, 16), amount);
}

bool TokenBurn::IsValid() const
{
    // AUDIT FIX [R28-02]: Same INT64_MAX cap as TokenTransfer (see above).
    return !token_id.IsNull() && amount > 0 &&
           amount <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
}

std::string TokenBurn::ToString() const
{
    return strprintf("TokenBurn{token=%s, amount=%llu}",
                     token_id.ToString().substr(0, 16), amount);
}

std::vector<uint8_t> GetOpReturnData(const CScript& script)
{
    std::vector<uint8_t> data;
    
    CScript::const_iterator pc = script.begin();
    opcodetype opcode;
    std::vector<unsigned char> vch;

    // First opcode should be OP_RETURN
    if (!script.GetOp(pc, opcode, vch)) {
        return data;
    }
    if (opcode != OP_RETURN) {
        return data;
    }

    // Get the data push
    if (!script.GetOp(pc, opcode, vch)) {
        return data;
    }

    // For direct push, the data is in vch
    if (opcode >= 0x01 && opcode <= 0x4b) {
        data.assign(vch.begin(), vch.end());
    }
    // For PUSHDATA1/2/4, the data is also in vch
    else if (opcode == OP_PUSHDATA1 || opcode == OP_PUSHDATA2 || opcode == OP_PUSHDATA4) {
        data.assign(vch.begin(), vch.end());
    }

    return data;
}

bool IsSRC20Script(const CScript& script)
{
    std::vector<uint8_t> data = GetOpReturnData(script);
    if (data.size() < SRC20_PROTOCOL_ID.size() + 2) { // ID + version + action minimum
        return false;
    }
    
    return std::equal(SRC20_PROTOCOL_ID.begin(), SRC20_PROTOCOL_ID.end(), data.begin());
}

std::optional<SRC20Operation> ParseSRC20Script(const CScript& script)
{
    std::vector<uint8_t> data = GetOpReturnData(script);
    
    // Minimum size: SRC20 (5) + version (1) + action (1) = 7 bytes
    if (data.size() < 7) {
        return std::nullopt;
    }

    // Check protocol identifier
    if (!std::equal(SRC20_PROTOCOL_ID.begin(), SRC20_PROTOCOL_ID.end(), data.begin())) {
        return std::nullopt;
    }

    size_t offset = SRC20_PROTOCOL_ID.size();

    // Check version - must be within supported range for forward/backward compatibility
    uint8_t version = data[offset++];
    if (version < SRC20_MIN_SUPPORTED_VERSION || version > SRC20_MAX_SUPPORTED_VERSION) {
        LogDebug(BCLog::TOKEN, "SRC20: Unsupported version %u (supported: %u-%u)\n", 
                 version, SRC20_MIN_SUPPORTED_VERSION, SRC20_MAX_SUPPORTED_VERSION);
        return std::nullopt;
    }

    // Parse action
    TokenAction action = ActionFromByte(data[offset++]);
    if (action == TokenAction::INVALID) {
        return std::nullopt;
    }

    SRC20Operation op;
    op.action = action;

    switch (action) {
        case TokenAction::ISSUE: {
            // ISSUE format: ticker(4) + name(32) + decimals(1) + supply(8) + metadata(32) = 77 bytes min
            if (data.size() < offset + 4) {
                return std::nullopt;
            }

            TokenIssuance issuance;
            
            // Read ticker (4 bytes, null-padded)
            // AUDIT FIX [R16-M01]: Stop at first null byte instead of stripping all nulls.
            // Previously "A\0BC" and "ABC\0" both decoded to "ABC", creating
            // ticker collision classes where different wire encodings map to the
            // same string. Now "A\0BC" decodes to "A" (stop at first null).
            size_t ticker_end = offset + MAX_TICKER_LENGTH;
            for (size_t i = offset; i < ticker_end && i < data.size(); ++i) {
                if (data[i] == 0) break;
                issuance.ticker += static_cast<char>(data[i]);
            }
            offset = ticker_end;

            if (data.size() < offset + MAX_NAME_LENGTH) {
                return std::nullopt;
            }

            // Read name (32 bytes, null-padded)
            // AUDIT FIX [R16-M01]: Stop at first null byte (same rationale as ticker).
            size_t name_end = offset + MAX_NAME_LENGTH;
            for (size_t i = offset; i < name_end && i < data.size(); ++i) {
                if (data[i] == 0) break;
                issuance.name += static_cast<char>(data[i]);
            }
            offset = name_end;

            if (data.size() < offset + 1 + 8 + 32) {
                return std::nullopt;
            }

            // Read decimals (1 byte)
            issuance.decimals = data[offset++];

            // Read supply (8 bytes, little-endian)
            issuance.total_supply = 0;
            for (int i = 0; i < 8; ++i) {
                issuance.total_supply |= static_cast<uint64_t>(data[offset++]) << (i * 8);
            }

            // Read metadata hash (32 bytes)
            std::vector<uint8_t> hash_data(data.begin() + offset, data.begin() + offset + 32);
            issuance.metadata_hash = uint256(hash_data);

            if (!issuance.IsValid()) {
                return std::nullopt;
            }

            op.data = issuance;
            break;
        }

        case TokenAction::TRANSFER: {
            // TRANSFER format: token_id(32) + amount(8) = 40 bytes
            if (data.size() < offset + 32 + 8) {
                return std::nullopt;
            }

            TokenTransfer transfer;

            // Read token ID (32 bytes)
            std::vector<uint8_t> id_data(data.begin() + offset, data.begin() + offset + 32);
            transfer.token_id = TokenId(uint256(id_data));
            offset += 32;

            // Read amount (8 bytes, little-endian)
            transfer.amount = 0;
            for (int i = 0; i < 8; ++i) {
                transfer.amount |= static_cast<uint64_t>(data[offset++]) << (i * 8);
            }

            if (!transfer.IsValid()) {
                return std::nullopt;
            }

            op.data = transfer;
            break;
        }

        case TokenAction::BURN: {
            // BURN format: token_id(32) + amount(8) = 40 bytes
            if (data.size() < offset + 32 + 8) {
                return std::nullopt;
            }

            TokenBurn burn;

            // Read token ID (32 bytes)
            std::vector<uint8_t> id_data(data.begin() + offset, data.begin() + offset + 32);
            burn.token_id = TokenId(uint256(id_data));
            offset += 32;

            // Read amount (8 bytes, little-endian)
            burn.amount = 0;
            for (int i = 0; i < 8; ++i) {
                burn.amount |= static_cast<uint64_t>(data[offset++]) << (i * 8);
            }

            if (!burn.IsValid()) {
                return std::nullopt;
            }

            op.data = burn;
            break;
        }

        default:
            return std::nullopt;
    }

    return op;
}

CScript BuildIssuanceScript(const TokenIssuance& issuance)
{
    std::vector<uint8_t> data;
    
    // Protocol ID
    data.insert(data.end(), SRC20_PROTOCOL_ID.begin(), SRC20_PROTOCOL_ID.end());
    
    // Version
    data.push_back(SRC20_VERSION);
    
    // Action
    data.push_back(static_cast<uint8_t>(TokenAction::ISSUE));
    
    // Ticker (4 bytes, null-padded)
    for (size_t i = 0; i < MAX_TICKER_LENGTH; ++i) {
        data.push_back(i < issuance.ticker.size() ? 
                       static_cast<uint8_t>(issuance.ticker[i]) : 0);
    }
    
    // Name (32 bytes, null-padded)
    for (size_t i = 0; i < MAX_NAME_LENGTH; ++i) {
        data.push_back(i < issuance.name.size() ? 
                       static_cast<uint8_t>(issuance.name[i]) : 0);
    }
    
    // Decimals (1 byte)
    data.push_back(issuance.decimals);
    
    // Supply (8 bytes, little-endian)
    for (int i = 0; i < 8; ++i) {
        data.push_back(static_cast<uint8_t>((issuance.total_supply >> (i * 8)) & 0xFF));
    }
    
    // Metadata hash (32 bytes)
    for (const auto& byte : issuance.metadata_hash) {
        data.push_back(byte);
    }
    
    CScript script;
    script << OP_RETURN;
    script << data;
    return script;
}

CScript BuildTransferScript(const TokenTransfer& transfer)
{
    std::vector<uint8_t> data;
    
    // Protocol ID
    data.insert(data.end(), SRC20_PROTOCOL_ID.begin(), SRC20_PROTOCOL_ID.end());
    
    // Version
    data.push_back(SRC20_VERSION);
    
    // Action
    data.push_back(static_cast<uint8_t>(TokenAction::TRANSFER));
    
    // Token ID (32 bytes)
    const auto& hash = transfer.token_id.GetHash();
    for (const auto& byte : hash) {
        data.push_back(byte);
    }
    
    // Amount (8 bytes, little-endian)
    for (int i = 0; i < 8; ++i) {
        data.push_back(static_cast<uint8_t>((transfer.amount >> (i * 8)) & 0xFF));
    }
    
    CScript script;
    script << OP_RETURN;
    script << data;
    return script;
}

CScript BuildBurnScript(const TokenBurn& burn)
{
    std::vector<uint8_t> data;
    
    // Protocol ID
    data.insert(data.end(), SRC20_PROTOCOL_ID.begin(), SRC20_PROTOCOL_ID.end());
    
    // Version
    data.push_back(SRC20_VERSION);
    
    // Action
    data.push_back(static_cast<uint8_t>(TokenAction::BURN));
    
    // Token ID (32 bytes)
    const auto& hash = burn.token_id.GetHash();
    for (const auto& byte : hash) {
        data.push_back(byte);
    }
    
    // Amount (8 bytes, little-endian)
    for (int i = 0; i < 8; ++i) {
        data.push_back(static_cast<uint8_t>((burn.amount >> (i * 8)) & 0xFF));
    }
    
    CScript script;
    script << OP_RETURN;
    script << data;
    return script;
}

std::vector<SRC20Operation> ParseTransactionSRC20(const CTransaction& tx)
{
    std::vector<SRC20Operation> ops;
    
    // AUDIT FIX [H-05]: Limit the number of SRC-20 operations per transaction.
    // Without this, a single transaction with hundreds of OP_RETURN outputs
    // could consume the entire block's token budget and cause O(n) DB lookups.
    // AUDIT FIX [R18-02]: Now uses the shared constant from the header.

    for (const auto& vout : tx.vout) {
        if (ops.size() >= MAX_OPS_PER_TX) break;
        if (IsSRC20Script(vout.scriptPubKey)) {
            auto op = ParseSRC20Script(vout.scriptPubKey);
            if (op) {
                ops.push_back(*op);
            }
        }
    }
    
    return ops;
}

std::optional<CScript> GetTransferRecipient(const CTransaction& tx)
{
    // A valid transfer transaction must have at least 2 outputs:
    // Output 0: OP_RETURN with SRC-20 data
    // Output 1: Recipient
    if (tx.vout.size() < 2) {
        return std::nullopt;
    }
    
    // First output should be SRC-20 transfer
    auto op = ParseSRC20Script(tx.vout[0].scriptPubKey);
    if (!op || op->action != TokenAction::TRANSFER) {
        return std::nullopt;
    }
    
    // AUDIT FIX [RECIPIENT-01]: Validate the recipient script.
    // Without this check, tokens could be transferred to an OP_RETURN or
    // other unspendable output, silently burning them without generating a
    // proper BURN record. An empty script is also rejected — it would result
    // in unrecoverable tokens with no valid owner.
    const CScript& recipient = tx.vout[1].scriptPubKey;
    if (recipient.empty() || recipient.IsUnspendable()) {
        return std::nullopt;
    }
    
    return recipient;
}

std::optional<CScript> GetTransferChange(const CTransaction& tx)
{
    // Change is optional third output
    if (tx.vout.size() < 3) {
        return std::nullopt;
    }
    
    // First output should be SRC-20 transfer
    auto op = ParseSRC20Script(tx.vout[0].scriptPubKey);
    if (!op || op->action != TokenAction::TRANSFER) {
        return std::nullopt;
    }
    
    // Third output is change
    // AUDIT FIX [R16-M03]: Validate change output is spendable, matching
    // the GetTransferRecipient validation. Without this, tokens could be
    // credited to an OP_RETURN or empty script via the change output,
    // silently destroying the sender's remaining token balance.
    const CScript& change = tx.vout[2].scriptPubKey;
    if (change.empty() || change.IsUnspendable()) {
        return std::nullopt;
    }
    return change;
}

namespace reserved {

// FIX L-05: Runtime-extensible reserved tickers list
static std::set<std::string> g_runtime_reserved_tickers;
static Mutex g_reserved_mutex;

bool IsReservedTicker(const std::string& ticker)
{
    // AUDIT FIX [I-02]: RESERVED_TICKERS is a constexpr static array initialized
    // at compile time, so iterating it without a lock is thread-safe.
    // Only g_runtime_reserved_tickers requires locking.
    for (const auto* reserved : RESERVED_TICKERS) {
        if (ticker == reserved) {
            return true;
        }
    }
    
    // Then check runtime-added tickers (for future governance)
    LOCK(g_reserved_mutex);
    return g_runtime_reserved_tickers.count(ticker) > 0;
}

void AddReservedTicker(const std::string& ticker)
{
    // AUDIT FIX [R22-FIX-02]: Validate ticker format before inserting.
    // Accepting arbitrary strings (lowercase, empty, overlength) wastes memory
    // and creates entries that can never match a valid token ticker.
    if (ticker.size() < MIN_TICKER_LENGTH || ticker.size() > MAX_TICKER_LENGTH) {
        LogPrintf("WARNING: AddReservedTicker ignored invalid ticker '%s' (wrong length)\n", ticker);
        return;
    }
    for (unsigned char c : ticker) {
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
            LogPrintf("WARNING: AddReservedTicker ignored invalid ticker '%s' (bad characters)\n", ticker);
            return;
        }
    }

    LOCK(g_reserved_mutex);
    auto [it, inserted] = g_runtime_reserved_tickers.insert(ticker);
    if (inserted) {
        LogPrintf("Reserved ticker added at runtime: %s\n", ticker);
    } else {
        LogDebug(BCLog::TOKEN, "AddReservedTicker: ticker '%s' was already reserved\n", ticker);
    }
}

std::vector<std::string> GetReservedTickers()
{
    // SECURITY FIX [H-03b]: Acquire lock before accessing g_runtime_reserved_tickers.size()
    // to prevent data race (undefined behavior per C++ standard). Previously the
    // .size() call for reserve() was done without the lock.
    std::vector<std::string> result;
    
    for (const auto* reserved : RESERVED_TICKERS) {
        result.emplace_back(reserved);
    }
    
    LOCK(g_reserved_mutex);
    result.reserve(result.size() + g_runtime_reserved_tickers.size());
    for (const auto& ticker : g_runtime_reserved_tickers) {
        result.push_back(ticker);
    }
    
    return result;
}

} // namespace reserved

namespace wellknown {

TokenIssuance CreateESYP()
{
    TokenIssuance issuance;
    // FIX [L-01b]: Use uppercase ticker to pass ticker validation.
    // Previously "eSYP" contained lowercase 'e' which fails IsValid().
    issuance.ticker = "ESYP";
    issuance.name = "Electronic Syrian Pound";
    issuance.decimals = 2;  // Pounds have 2 decimal places (piasters)
    issuance.total_supply = 100000000000ULL * 100;  // 100 billion SYP with 2 decimals
    issuance.metadata_hash.SetNull();  // To be set on actual issuance
    return issuance;
}

TokenIssuance CreateSUSDT()
{
    TokenIssuance issuance;
    // FIX [L-01b]: Use uppercase ticker to pass ticker validation.
    // Previously "sUST" contained lowercase 's' which fails IsValid().
    issuance.ticker = "SUST";
    issuance.name = "Synthetic USDT";
    issuance.decimals = 6;  // Same as USDT
    // AUDIT FIX [R16-C01]: Reduced supply to pass IsValid() overflow check.
    // Previously 10^15 exceeded max_safe_supply (UINT64_MAX / 10^6 ≈ 1.8×10^13).
    // New value: 1 billion SUST = 10^9 * 10^6 = 10^15 smallest units → STILL fails.
    // Correct: 1 billion SUST in display units = 10^9 tokens. But total_supply is
    // already in smallest units, so set to 10^9 (1 billion smallest units = 1000 SUST).
    // For 1 billion display-unit SUST, we'd need total_supply = 10^15, which overflows.
    // Settle on 10 billion display-unit SUST → total_supply = 10^10 * 10^6 = 10^16 → still too big.
    // Max safe: 18,446,744,073,709 smallest units ≈ 18.4 million display SUST.
    // Use 10 million display SUST = 10^7 * 10^6 = 10^13 smallest units (within limit).
    issuance.total_supply = 10000000ULL * 1000000;  // 10 million SUST with 6 decimals
    issuance.metadata_hash.SetNull();  // To be set on actual issuance
    return issuance;
}

} // namespace wellknown

} // namespace src20
