// Copyright (c) 2024-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENSY_SCRIPT_SRC20_H
#define OPENSY_SCRIPT_SRC20_H

#include <primitives/transaction.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace src20 {

/**
 * ═══════════════════════════════════════════════════════════════════════════════
 * SRC-20 TOKEN PROTOCOL SPECIFICATION
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * SRC-20 is OpenSY's native token standard, inspired by BRC-20 but designed for
 * a UTXO-based blockchain with OP_RETURN data embedding.
 *
 * PROTOCOL FORMAT (OP_RETURN):
 *   Byte 0-4:   "SRC20" - Protocol identifier
 *   Byte 5:     Version (currently 1)
 *   Byte 6:     Action (0x01=ISSUE, 0x02=TRANSFER, 0x03=BURN)
 *   Byte 7+:    Action-specific data (see below)
 *
 * TOKEN ISSUANCE (Action 0x01):
 *   Bytes 7-10:   Ticker (4 bytes, uppercase ASCII, space-padded)
 *   Bytes 11-42:  Name (32 bytes, UTF-8, null-padded)
 *   Byte 43:      Decimals (0-18)
 *   Bytes 44-51:  Total supply (8 bytes, little-endian)
 *   Bytes 52-83:  Metadata hash (32 bytes, optional IPFS CID or zero)
 *
 * TOKEN TRANSFER (Action 0x02):
 *   Bytes 7-38:   Token ID (32 bytes)
 *   Bytes 39-46:  Amount (8 bytes, little-endian)
 *   Bytes 47+:    Recipient address (variable, serialized CScript)
 *
 * TOKEN BURN (Action 0x03):
 *   Bytes 7-38:   Token ID (32 bytes)
 *   Bytes 39-46:  Amount (8 bytes, little-endian)
 *
 * VALIDATION RULES:
 *   - Token IDs are derived from the issuance transaction hash
 *   - Transfers require sender to have sufficient balance
 *   - Burns permanently remove tokens from circulation
 *   - Maximum 100 token operations per block (spam prevention)
 *
 * VERSIONING:
 *   The protocol version byte allows future upgrades while maintaining
 *   backward compatibility. Version 1 clients MUST reject unknown versions.
 *
 * ═══════════════════════════════════════════════════════════════════════════════
 */

/** SRC-20 Protocol version - increment on breaking protocol changes
 *
 *  VERSION HISTORY:
 *    v1 (current): Initial release with ISSUE, TRANSFER, BURN operations
 *
 *  UPGRADE PROCEDURE:
 *    1. Increment SRC20_VERSION for the new protocol
 *    2. Update SRC20_MAX_SUPPORTED_VERSION to accept new version
 *    3. Keep SRC20_MIN_SUPPORTED_VERSION at 1 for backward compatibility
 *    4. Add version-specific parsing logic in ParseSRC20Script()
 *
 *  BACKWARD COMPATIBILITY:
 *    Nodes accept operations with version in [MIN, MAX] range.
 *    When MIN < MAX, nodes can validate both old and new format tokens.
 */
static constexpr uint8_t SRC20_VERSION = 1;

/** Minimum supported protocol version for backward compatibility
 *  Lowering this would accept older protocol formats (if they existed) */
static constexpr uint8_t SRC20_MIN_SUPPORTED_VERSION = 1;

/** Maximum supported protocol version
 *  Raising this enables acceptance of newer protocol formats after upgrade */
static constexpr uint8_t SRC20_MAX_SUPPORTED_VERSION = 1;

/** Maximum tokens allowed per block (spam prevention) */
static constexpr size_t MAX_TOKENS_PER_BLOCK = 100;

/** Protocol identifier in OP_RETURN */
static constexpr std::array<uint8_t, 5> SRC20_PROTOCOL_ID = {'S', 'R', 'C', '2', '0'};

/** Maximum ticker length (4 bytes) */
static constexpr size_t MAX_TICKER_LENGTH = 4;

/** Maximum name length (32 bytes) */
static constexpr size_t MAX_NAME_LENGTH = 32;

/** Maximum decimals (18, same as ETH) */
static constexpr uint8_t MAX_DECIMALS = 18;

/** Token action types */
enum class TokenAction : uint8_t {
    ISSUE = 0x01,
    TRANSFER = 0x02,
    BURN = 0x03,
    INVALID = 0xFF
};

/** Get string representation of token action */
std::string ActionToString(TokenAction action);

/** Parse action from byte */
TokenAction ActionFromByte(uint8_t byte);

/**
 * SRC-20 Token ID
 * 
 * The token ID is derived from the hash of the issuance transaction.
 * This ensures uniqueness and prevents ID collisions.
 */
class TokenId {
private:
    uint256 m_hash;

public:
    TokenId() : m_hash() {}
    explicit TokenId(const uint256& hash) : m_hash(hash) {}
    explicit TokenId(const Txid& txid) : m_hash(txid.IsNull() ? uint256{} : uint256{txid.ToUint256()}) {}

    const uint256& GetHash() const { return m_hash; }
    bool IsNull() const { return m_hash.IsNull(); }
    void SetNull() { m_hash.SetNull(); }

    std::string ToString() const { return m_hash.ToString(); }
    std::string GetHex() const { return m_hash.GetHex(); }

    static std::optional<TokenId> FromHex(const std::string& hex);

    friend bool operator==(const TokenId& a, const TokenId& b) { return a.m_hash == b.m_hash; }
    friend bool operator!=(const TokenId& a, const TokenId& b) { return a.m_hash != b.m_hash; }
    friend bool operator<(const TokenId& a, const TokenId& b) { return a.m_hash < b.m_hash; }

    SERIALIZE_METHODS(TokenId, obj) {
        READWRITE(obj.m_hash);
    }
};

/**
 * Token issuance data
 * 
 * Format: OP_RETURN "SRC20" 0x01 <ticker:4> <name:32> <decimals:1> <supply:8> <metadata_hash:32>
 */
struct TokenIssuance {
    std::string ticker;           // 4 bytes max
    std::string name;             // 32 bytes max
    uint8_t decimals;             // 0-18
    uint64_t total_supply;        // Total supply in smallest units
    uint256 metadata_hash;        // IPFS hash or other metadata reference

    TokenIssuance() : decimals(0), total_supply(0) {}
    
    bool IsValid() const;
    std::string ToString() const;

    SERIALIZE_METHODS(TokenIssuance, obj) {
        READWRITE(obj.ticker, obj.name, obj.decimals, obj.total_supply, obj.metadata_hash);
    }
};

/**
 * Token transfer data
 * 
 * Format: OP_RETURN "SRC20" 0x02 <token_id:32> <amount:8>
 * Output 1: Recipient address
 * Output 2: Change address (remaining tokens)
 */
struct TokenTransfer {
    TokenId token_id;
    uint64_t amount;

    TokenTransfer() : amount(0) {}
    TokenTransfer(const TokenId& id, uint64_t amt) : token_id(id), amount(amt) {}

    bool IsValid() const;
    std::string ToString() const;

    SERIALIZE_METHODS(TokenTransfer, obj) {
        READWRITE(obj.token_id, obj.amount);
    }
};

/**
 * Token burn data
 * 
 * Format: OP_RETURN "SRC20" 0x03 <token_id:32> <amount:8>
 */
struct TokenBurn {
    TokenId token_id;
    uint64_t amount;

    TokenBurn() : amount(0) {}
    TokenBurn(const TokenId& id, uint64_t amt) : token_id(id), amount(amt) {}

    bool IsValid() const;
    std::string ToString() const;

    SERIALIZE_METHODS(TokenBurn, obj) {
        READWRITE(obj.token_id, obj.amount);
    }
};

/**
 * Parsed SRC-20 operation
 */
struct SRC20Operation {
    TokenAction action;
    std::variant<TokenIssuance, TokenTransfer, TokenBurn> data;

    SRC20Operation() : action(TokenAction::INVALID) {}

    bool IsValid() const { return action != TokenAction::INVALID; }
    
    const TokenIssuance* GetIssuance() const {
        return std::holds_alternative<TokenIssuance>(data) ? 
               &std::get<TokenIssuance>(data) : nullptr;
    }
    
    const TokenTransfer* GetTransfer() const {
        return std::holds_alternative<TokenTransfer>(data) ? 
               &std::get<TokenTransfer>(data) : nullptr;
    }
    
    const TokenBurn* GetBurn() const {
        return std::holds_alternative<TokenBurn>(data) ? 
               &std::get<TokenBurn>(data) : nullptr;
    }
};

/**
 * Parse SRC-20 data from an OP_RETURN script
 * 
 * @param script The script to parse
 * @return Parsed operation, or nullopt if not a valid SRC-20 script
 */
std::optional<SRC20Operation> ParseSRC20Script(const CScript& script);

/**
 * Check if a script is an SRC-20 OP_RETURN
 * 
 * @param script The script to check
 * @return true if script starts with SRC-20 protocol identifier
 */
bool IsSRC20Script(const CScript& script);

/**
 * Get the OP_RETURN data from a script
 * 
 * @param script The script to extract data from
 * @return The data after OP_RETURN, or empty vector if not OP_RETURN
 */
std::vector<uint8_t> GetOpReturnData(const CScript& script);

/**
 * Build an SRC-20 issuance script
 * 
 * @param issuance The token issuance data
 * @return The OP_RETURN script for issuance
 */
CScript BuildIssuanceScript(const TokenIssuance& issuance);

/**
 * Build an SRC-20 transfer script
 * 
 * @param transfer The token transfer data
 * @return The OP_RETURN script for transfer
 */
CScript BuildTransferScript(const TokenTransfer& transfer);

/**
 * Build an SRC-20 burn script
 * 
 * @param burn The token burn data
 * @return The OP_RETURN script for burn
 */
CScript BuildBurnScript(const TokenBurn& burn);

/**
 * Parse token operations from a transaction
 * 
 * @param tx The transaction to parse
 * @return Vector of SRC-20 operations found in the transaction
 */
std::vector<SRC20Operation> ParseTransactionSRC20(const CTransaction& tx);

/**
 * Get recipient address from a transfer transaction
 * 
 * A valid transfer must have:
 * - Output 0: OP_RETURN with SRC-20 transfer data
 * - Output 1: Recipient (gets tokens)
 * - Output 2 (optional): Change (remaining tokens)
 * 
 * @param tx The transaction
 * @return Recipient script, or nullopt if invalid format
 */
std::optional<CScript> GetTransferRecipient(const CTransaction& tx);

/**
 * Get change address from a transfer transaction
 * 
 * @param tx The transaction
 * @return Change script, or nullopt if no change output
 */
std::optional<CScript> GetTransferChange(const CTransaction& tx);

/**
 * Reserved token tickers for OpenSY ecosystem
 * FIX L-05: Extended list with common variations to prevent confusion/squatting
 */
namespace reserved {
    // Core reserved tickers - these will never be available for user tokens
    // FIX [L-09]: Removed entries with lowercase characters (eSYP, sUSD, sUST)
    // since ticker validation only allows uppercase A-Z and 0-9, making lowercase
    // reservations dead code. Also removed OPENSY (6 chars > MAX_TICKER_LENGTH=4).
    static constexpr std::array<const char*, 8> RESERVED_TICKERS = {
        // Native coin and variations
        "SYL",   // Native coin (not a token, but reserved)
        "OSYL",  // OpenSYL variation
        
        // Official stablecoins (uppercase only — matches ticker validation)
        "ESYP",  // Electronic Syrian Pound
        "SUSD",  // Synthetic USD
        "SUST",  // Synthetic USDT
        
        // Prevent impersonation
        "BTC",   // Bitcoin
        "ETH",   // Ethereum
        "USDT"   // Tether
    };

    /** Check if a ticker is reserved */
    bool IsReservedTicker(const std::string& ticker);
    
    /** Add a ticker to the reserved list at runtime (for future governance) */
    void AddReservedTicker(const std::string& ticker);
    
    /** Get all reserved tickers (for RPC/debugging) */
    std::vector<std::string> GetReservedTickers();
}

/**
 * Well-known token configurations for genesis
 */
namespace wellknown {
    /** eSYP - Electronic Syrian Pound */
    TokenIssuance CreateESYP();
    
    /** sUSDT - Synthetic USDT for internal use */
    TokenIssuance CreateSUSDT();
}

} // namespace src20

#endif // OPENSY_SCRIPT_SRC20_H
