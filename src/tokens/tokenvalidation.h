// Copyright (c) 2024-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENSY_TOKENS_TOKENVALIDATION_H
#define OPENSY_TOKENS_TOKENVALIDATION_H

#include <primitives/transaction.h>
#include <script/src20.h>
#include <tokens/tokendb.h>
#include <validation.h>

#include <optional>
#include <string>
#include <vector>

namespace tokens {

/**
 * Token validation error codes
 */
enum class TokenValidationResult {
    OK,
    INVALID_FORMAT,           // Invalid SRC-20 data format
    INVALID_VERSION,          // Unsupported protocol version
    INVALID_ACTION,           // Unknown action type
    INVALID_TICKER,           // Invalid ticker format
    RESERVED_TICKER,          // Ticker is reserved
    DUPLICATE_TICKER,         // Ticker already exists
    TOKEN_NOT_FOUND,          // Token ID doesn't exist
    INSUFFICIENT_BALANCE,     // Not enough tokens
    INVALID_AMOUNT,           // Amount is zero or negative
    MISSING_RECIPIENT,        // Transfer missing recipient output
    BLOCK_TOKEN_LIMIT,        // Too many tokens in block
    INTERNAL_ERROR,           // Database or internal error
};

/**
 * Get human-readable error message
 */
std::string TokenValidationResultToString(TokenValidationResult result);

/**
 * Result of token validation
 */
struct TokenValidation {
    TokenValidationResult result;
    std::string message;
    std::optional<src20::SRC20Operation> operation;

    TokenValidation() : result(TokenValidationResult::OK) {}
    TokenValidation(TokenValidationResult r) : result(r), message(TokenValidationResultToString(r)) {}
    TokenValidation(TokenValidationResult r, const std::string& msg) : result(r), message(msg) {}

    bool IsValid() const { return result == TokenValidationResult::OK; }
    operator bool() const { return IsValid(); }
};

/**
 * Token validator for mempool and block validation
 */
class TokenValidator {
private:
    const TokenDB& m_tokendb;

    // Validate issuance operation
    TokenValidation ValidateIssuance(const src20::TokenIssuance& issuance) const;
    
    // Validate transfer operation
    TokenValidation ValidateTransfer(
        const src20::TokenTransfer& transfer,
        const CTransaction& tx,
        const CScript& sender) const;
    
    // Validate burn operation
    TokenValidation ValidateBurn(
        const src20::TokenBurn& burn,
        const CScript& sender) const;

public:
    explicit TokenValidator(const TokenDB& tokendb) : m_tokendb(tokendb) {}

    /**
     * Validate a single SRC-20 operation
     * 
     * @param op The operation to validate
     * @param tx The containing transaction
     * @param sender The sender address (from spent UTXO)
     * @return Validation result
     */
    TokenValidation ValidateOperation(
        const src20::SRC20Operation& op,
        const CTransaction& tx,
        const CScript& sender) const;

    /**
     * Validate all SRC-20 operations in a transaction
     * 
     * @param tx The transaction to validate
     * @param sender The sender address (from spent UTXO)
     * @return Vector of validation results (one per operation)
     */
    std::vector<TokenValidation> ValidateTransaction(
        const CTransaction& tx,
        const CScript& sender) const;

    /**
     * Validate token operations in a block
     * 
     * Checks:
     * - All individual operations are valid
     * - Max tokens per block limit not exceeded
     * - No conflicting operations within block
     * 
     * @param block The block to validate
     * @param height Block height
     * @return Validation result (first error encountered, or OK)
     */
    TokenValidation ValidateBlock(const CBlock& block, int height) const;

    /**
     * Check if a transaction can be added to mempool from token perspective
     * 
     * @param tx The transaction
     * @param sender The sender address
     * @return true if all token operations are valid
     */
    bool CheckMempoolAccept(const CTransaction& tx, const CScript& sender) const;
};

/**
 * Mempool token state tracker
 * 
 * Tracks pending token operations in mempool to prevent conflicts
 * before they're confirmed in a block.
 * 
 * Includes DoS protection via rate limiting per address.
 */
class MempoolTokenState {
public:
    // DoS protection constants
    static constexpr size_t MAX_PENDING_OPS_PER_ADDRESS = 10;      // Max pending ops per address
    static constexpr size_t MAX_PENDING_ISSUANCES = 100;           // Max pending issuances total
    static constexpr size_t MAX_TOTAL_PENDING_OPS = 1000;          // Max total pending operations
    static constexpr int64_t RATE_LIMIT_WINDOW_SECONDS = 600;      // 10 minute window
    static constexpr size_t MAX_OPS_PER_ADDRESS_PER_WINDOW = 50;   // Max ops per address per window

private:
    mutable Mutex m_cs;
    
    // Pending issuances by ticker (prevents duplicate tickers in mempool)
    std::map<std::string, uint256> m_pending_tickers GUARDED_BY(m_cs);
    
    // Pending balance changes per address/token
    // Maps: address -> token_id -> delta (can be negative)
    std::map<CScript, std::map<src20::TokenId, int64_t>> m_pending_balances GUARDED_BY(m_cs);
    
    // Transaction -> operations mapping for removal
    std::map<uint256, std::vector<src20::SRC20Operation>> m_tx_ops GUARDED_BY(m_cs);
    
    // FIX 4.1: Store per-transaction balance deltas for proper reversal
    // Maps: txid -> (address -> (token_id -> delta))
    // This allows RemoveTransaction to properly reverse balance changes
    struct TxBalanceDeltas {
        std::map<CScript, std::map<src20::TokenId, int64_t>> deltas;
    };
    std::map<uint256, TxBalanceDeltas> m_tx_balance_deltas GUARDED_BY(m_cs);
    
    // Track sender for each transaction (for DoS tracking cleanup)
    std::map<uint256, CScript> m_tx_sender GUARDED_BY(m_cs);
    
    // DoS protection: Track operations per address
    // Maps: address -> count of pending operations
    std::map<CScript, size_t> m_ops_per_address GUARDED_BY(m_cs);
    
    // Rate limiting: Track operations per address in time window
    // Maps: address -> list of timestamps
    std::map<CScript, std::vector<int64_t>> m_rate_limit_timestamps GUARDED_BY(m_cs);
    
    /**
     * Check if an address is rate limited
     * @param address The address to check
     * @param now Current timestamp
     * @return true if rate limited (should reject)
     */
    bool IsRateLimited(const CScript& address, int64_t now) const EXCLUSIVE_LOCKS_REQUIRED(m_cs);
    
    /**
     * Record an operation for rate limiting
     * @param address The address
     * @param now Current timestamp
     */
    void RecordOperation(const CScript& address, int64_t now) EXCLUSIVE_LOCKS_REQUIRED(m_cs);

public:
    /**
     * Check if a transaction can be accepted based on DoS limits
     * 
     * @param tx The transaction
     * @param sender Sender address
     * @return Empty string if OK, error message if rejected
     */
    std::string CheckDoSLimits(const CTransaction& tx, const CScript& sender) const EXCLUSIVE_LOCKS_REQUIRED(!m_cs);
    
    /**
     * Add a transaction's token operations to pending state
     * 
     * @param tx The transaction
     * @param sender Sender address
     * @return true if successfully added
     */
    bool AddTransaction(const CTransaction& tx, const CScript& sender) EXCLUSIVE_LOCKS_REQUIRED(!m_cs);

    /**
     * Remove a transaction's token operations from pending state
     * 
     * @param txid Transaction ID to remove
     */
    void RemoveTransaction(const uint256& txid) EXCLUSIVE_LOCKS_REQUIRED(!m_cs);

    /**
     * Check if a ticker is pending in mempool
     */
    bool IsTickerPending(const std::string& ticker) const EXCLUSIVE_LOCKS_REQUIRED(!m_cs);

    /**
     * Get pending balance delta for an address/token
     * 
     * @return The pending change (positive or negative)
     */
    int64_t GetPendingBalanceDelta(const CScript& address, const src20::TokenId& token_id) const EXCLUSIVE_LOCKS_REQUIRED(!m_cs);

    /**
     * Get effective balance (confirmed + pending)
     */
    uint64_t GetEffectiveBalance(
        const TokenDB& tokendb,
        const CScript& address,
        const src20::TokenId& token_id) const EXCLUSIVE_LOCKS_REQUIRED(!m_cs);

    /**
     * Check if a transfer would be valid considering mempool state
     */
    bool CanTransfer(
        const TokenDB& tokendb,
        const CScript& from,
        const src20::TokenId& token_id,
        uint64_t amount) const EXCLUSIVE_LOCKS_REQUIRED(!m_cs);

    /**
     * Clear all pending state (e.g., on new block)
     */
    void Clear() EXCLUSIVE_LOCKS_REQUIRED(!m_cs);

    /**
     * Get count of pending token transactions
     */
    size_t GetPendingCount() const EXCLUSIVE_LOCKS_REQUIRED(!m_cs);
    
    /**
     * Get count of pending operations for an address (for DoS stats)
     */
    size_t GetPendingOpsForAddress(const CScript& address) const EXCLUSIVE_LOCKS_REQUIRED(!m_cs);
    
    /**
     * AUDIT FIX: Garbage collect stale rate limit entries
     * 
     * Removes entries from m_rate_limit_timestamps that have no recent activity.
     * Should be called periodically (e.g., on each block) to prevent unbounded memory growth
     * from many unique addresses sending single token operations.
     * 
     * @return Number of addresses pruned
     */
    size_t PruneStaleRateLimitEntries() EXCLUSIVE_LOCKS_REQUIRED(!m_cs);
};

/**
 * Global mempool token state
 */
extern std::unique_ptr<MempoolTokenState> g_mempool_tokens;

/**
 * Initialize mempool token tracking
 */
void InitMempoolTokenState();

/**
 * Shutdown mempool token tracking
 */
void ShutdownMempoolTokenState();

/**
 * Consensus-level token validation
 * 
 * Called during block validation to check token operations.
 * Note: Invalid token operations don't make the block invalid,
 * they're simply ignored (soft-fork style).
 */
class ConsensusTokenValidator {
public:
    /**
     * Count token operations in a block
     * 
     * @param block The block to count
     * @return Number of token operations (for spam limit)
     */
    static size_t CountTokenOperations(const CBlock& block);

    /**
     * Check if block exceeds token operation limit
     * 
     * @param block The block to check
     * @return true if within limits
     */
    static bool CheckBlockTokenLimit(const CBlock& block);

    /**
     * Get all valid token operations from a block
     * 
     * Invalid operations are filtered out (not consensus failures).
     * 
     * @param block The block
     * @param tokendb Token database for validation
     * @param view Coins view for looking up spent UTXOs (sender addresses)
     * @return Vector of valid operations with their transactions
     */
    static std::vector<std::pair<uint256, src20::SRC20Operation>> 
    GetValidOperations(const CBlock& block, const TokenDB& tokendb, const CCoinsViewCache* view = nullptr);
};

} // namespace tokens

#endif // OPENSY_TOKENS_TOKENVALIDATION_H
