// Copyright (c) 2024-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENSY_TOKENS_TOKENDB_H
#define OPENSY_TOKENS_TOKENDB_H

#include <coins.h>
#include <dbwrapper.h>
#include <primitives/block.h>
#include <script/src20.h>
#include <sync.h>
#include <uint256.h>
#include <undo.h>
#include <util/lru_cache.h>

#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace tokens {

/**
 * Token information stored in the database
 */
struct TokenInfo {
    src20::TokenId token_id;
    std::string ticker;
    std::string name;
    uint8_t decimals;
    uint64_t total_supply;
    uint64_t circulating_supply;  // Supply minus burned tokens
    uint256 metadata_hash;
    
    // Issuance information
    uint256 issuance_txid;
    int issuance_height;
    int64_t issuance_time;
    
    // Statistics
    uint64_t holder_count;
    uint64_t transfer_count;

    TokenInfo() : decimals(0), total_supply(0), circulating_supply(0),
                  issuance_height(0), issuance_time(0), 
                  holder_count(0), transfer_count(0) {}

    bool IsValid() const { return !token_id.IsNull(); }

    SERIALIZE_METHODS(TokenInfo, obj) {
        READWRITE(obj.token_id, obj.ticker, obj.name, obj.decimals,
                  obj.total_supply, obj.circulating_supply, obj.metadata_hash,
                  obj.issuance_txid, obj.issuance_height, obj.issuance_time,
                  obj.holder_count, obj.transfer_count);
    }
};

/**
 * Token balance for an address
 */
struct TokenBalance {
    src20::TokenId token_id;
    CScript address;
    uint64_t balance;

    TokenBalance() : balance(0) {}
    TokenBalance(const src20::TokenId& id, const CScript& addr, uint64_t bal)
        : token_id(id), address(addr), balance(bal) {}

    SERIALIZE_METHODS(TokenBalance, obj) {
        READWRITE(obj.token_id, obj.address, obj.balance);
    }
};

/**
 * Token transfer record
 */
struct TokenTransferRecord {
    src20::TokenId token_id;
    uint256 txid;
    CScript from_address;
    CScript to_address;
    uint64_t amount;
    int height;
    int64_t time;

    TokenTransferRecord() : amount(0), height(0), time(0) {}

    SERIALIZE_METHODS(TokenTransferRecord, obj) {
        READWRITE(obj.token_id, obj.txid, obj.from_address, obj.to_address,
                  obj.amount, obj.height, obj.time);
    }
};

/**
 * Token operation type for undo records
 */
enum class TokenOpType : uint8_t {
    ISSUE = 1,
    TRANSFER = 2,
    BURN = 3
};

/**
 * Undo record for reverting token operations during blockchain reorganization
 */
struct TokenUndoRecord {
    TokenOpType op_type;
    uint256 txid;
    src20::TokenId token_id;
    
    // For ISSUE: the entire token info to delete
    std::string ticker;  // To remove from ticker index
    
    // For TRANSFER: restore previous balances
    CScript from_address;
    CScript to_address;
    uint64_t amount;
    uint64_t prev_from_balance;  // Balance before transfer
    uint64_t prev_to_balance;    // Balance before transfer
    uint64_t prev_holder_count;  // Holder count before
    
    // For BURN: restore burned tokens
    uint64_t prev_circulating_supply;

    TokenUndoRecord() : op_type(TokenOpType::ISSUE), amount(0), 
                        prev_from_balance(0), prev_to_balance(0),
                        prev_holder_count(0), prev_circulating_supply(0) {}

    SERIALIZE_METHODS(TokenUndoRecord, obj) {
        // Serialize enum as uint8_t
        uint8_t op_type_u8{static_cast<uint8_t>(obj.op_type)};
        READWRITE(op_type_u8);
        // AUDIT FIX [M-01]: Validate op_type on deserialization to catch DB corruption.
        // Without this, corrupted undo records silently produce invalid op_types,
        // leading to incorrect reorg behavior.
        SER_READ(obj, {
            if (op_type_u8 < static_cast<uint8_t>(TokenOpType::ISSUE) ||
                op_type_u8 > static_cast<uint8_t>(TokenOpType::BURN)) {
                throw std::ios_base::failure("Invalid TokenUndoRecord op_type: " + std::to_string(op_type_u8));
            }
            obj.op_type = static_cast<TokenOpType>(op_type_u8);
        });
        READWRITE(obj.txid, obj.token_id, obj.ticker,
                  obj.from_address, obj.to_address, obj.amount,
                  obj.prev_from_balance, obj.prev_to_balance,
                  obj.prev_holder_count, obj.prev_circulating_supply);
    }
};

/**
 * Database key prefixes for token data
 */
namespace db_prefix {
    static constexpr uint8_t TOKEN_INFO = 'T';      // T<token_id> -> TokenInfo
    static constexpr uint8_t TOKEN_TICKER = 't';    // t<ticker> -> token_id
    static constexpr uint8_t UNDO = 'U';            // U<height><txid> -> TokenUndoRecord
    static constexpr uint8_t BALANCE = 'B';         // B<address><token_id> -> balance
    static constexpr uint8_t ADDR_TOKENS = 'A';     // A<address> -> set of token_ids
    static constexpr uint8_t TOKEN_HOLDERS = 'H';   // H<token_id><address> -> balance
    static constexpr uint8_t TRANSFER = 'X';        // X<token_id><height><txid> -> TransferRecord
    static constexpr uint8_t BLOCK_TOKENS = 'b';    // b<height> -> list of token ops in block
    static constexpr uint8_t BEST_BLOCK = 'Z';       // Z -> Best indexed block hash (distinct from BALANCE 'B')
    // UNDO prefix is defined above with other prefixes
}

/**
 * Token database for tracking SRC-20 token state
 * 
 * This database maintains:
 * - Token metadata (name, supply, decimals)
 * - Token balances per address
 * - Transfer history
 * - Holder information
 */
/**
 * AUDIT FIX [C-01/C-02]: In-memory balance overlay for batch processing.
 *
 * During ProcessBlock, multiple token operations may affect the same
 * address+token pair. Without an overlay, each operation reads stale
 * balances from the DB (batch not yet committed), enabling double-spend.
 *
 * The overlay tracks pending balance/supply/holder/addr_tokens deltas,
 * ensuring each operation sees the cumulative effect of prior operations
 * within the same block.
 */
struct BalanceOverlay {
    //! Pending balance state: key=(address, token_id) -> current balance
    std::map<std::pair<std::vector<unsigned char>, src20::TokenId>, uint64_t> balances;
    //! Pending addr_tokens sets: key=address -> set of token_ids
    std::map<std::vector<unsigned char>, std::set<src20::TokenId>> addr_tokens;
    //! Pending token info updates: key=token_id -> TokenInfo
    std::map<src20::TokenId, TokenInfo> token_infos;
    //! Pending tickers: key=ticker -> true (for dupe detection within a block)
    std::set<std::string> pending_tickers;
};

class TokenDB {
private:
    std::unique_ptr<CDBWrapper> m_db;
    mutable Mutex m_cs;

    // AUDIT FIX M-04: Cached token count to avoid full DB scan on each GetTokenCount() call
    mutable std::atomic<uint64_t> m_token_count_cache{0};
    mutable std::atomic<bool> m_token_count_initialized{false};

    // FIX L-04: Cached transfer count to avoid full DB scan on each GetTransferCount() call
    mutable std::atomic<uint64_t> m_transfer_count_cache{0};
    mutable std::atomic<bool> m_transfer_count_initialized{false};

    /**
     * LRU cache for frequently accessed token info.
     * 
     * Uses proper LRU eviction: least recently accessed tokens are evicted first.
     * This is more effective than the previous deterministic eviction which
     * could evict frequently-used tokens with low TokenIds.
     * 
     * Thread-safety: Protected by m_cs mutex.
     */
    struct TokenIdHasher {
        std::size_t operator()(const src20::TokenId& id) const noexcept {
            // Use first 8 bytes of the hash for fast hashing
            return static_cast<std::size_t>(id.GetHash().GetUint64(0));
        }
    };
    mutable LRUCache<src20::TokenId, TokenInfo, TokenIdHasher> m_token_cache GUARDED_BY(m_cs);
    static constexpr size_t MAX_CACHE_SIZE = 1000;

    // Helper methods
    bool WriteTokenInfo(const TokenInfo& info) EXCLUSIVE_LOCKS_REQUIRED(!m_cs);
    bool ReadTokenInfo(const src20::TokenId& token_id, TokenInfo& info) const EXCLUSIVE_LOCKS_REQUIRED(!m_cs);
    bool WriteBalance(const CScript& address, const src20::TokenId& token_id, uint64_t balance);
    bool ReadBalance(const CScript& address, const src20::TokenId& token_id, uint64_t& balance) const;
    
    /** Write balance to a batch for atomic multi-operation writes (e.g., reorg handling) */
    void WriteBalanceToBatch(CDBBatch& batch, const CScript& address, const src20::TokenId& token_id, uint64_t balance);
    
    /** Write token info to a batch for atomic writes */
    void WriteTokenInfoToBatch(CDBBatch& batch, const TokenInfo& info, bool update_cache = true) EXCLUSIVE_LOCKS_REQUIRED(!m_cs);
    
    void InvalidateCache(const src20::TokenId& token_id) const EXCLUSIVE_LOCKS_REQUIRED(!m_cs);
    void UpdateCache(const TokenInfo& info) const EXCLUSIVE_LOCKS_REQUIRED(!m_cs);

public:
    /**
     * Construct the token database
     * 
     * @param path Path to database directory
     * @param cache_size Cache size in bytes
     * @param memory Use in-memory database (for testing)
     * @param wipe Wipe existing data
     */
    TokenDB(const fs::path& path, size_t cache_size, bool memory = false, bool wipe = false);
    ~TokenDB();

    // Prevent copying
    TokenDB(const TokenDB&) = delete;
    TokenDB& operator=(const TokenDB&) = delete;

    /**
     * Check if database is usable
     */
    bool IsValid() const { return m_db != nullptr; }

    // ----- Token Information -----

    /**
     * Register a new token from an issuance transaction
     * 
     * @param issuance Token issuance data
     * @param txid Transaction ID of the issuance
     * @param issuer_address Address that issued the token
     * @param height Block height
     * @param time Block time
     * @return The token ID if successful
     */
    std::optional<src20::TokenId> RegisterToken(
        const src20::TokenIssuance& issuance,
        const uint256& txid,
        const CScript& issuer_address,
        int height,
        int64_t time,
        CDBBatch* external_batch = nullptr) EXCLUSIVE_LOCKS_REQUIRED(!m_cs);

    /**
     * Get token information by ID
     */
    std::optional<TokenInfo> GetTokenInfo(const src20::TokenId& token_id) const EXCLUSIVE_LOCKS_REQUIRED(!m_cs);

    /**
     * Get token information by ticker
     */
    std::optional<TokenInfo> GetTokenByTicker(const std::string& ticker) const EXCLUSIVE_LOCKS_REQUIRED(!m_cs);

    /**
     * Check if a token exists
     */
    bool TokenExists(const src20::TokenId& token_id) const EXCLUSIVE_LOCKS_REQUIRED(!m_cs);

    /**
     * Check if a ticker is already in use
     */
    bool TickerExists(const std::string& ticker) const;

    /**
     * Get all registered tokens
     * 
     * @param start Optional token ID to start from (for pagination)
     * @param count Maximum number of tokens to return
     */
    std::vector<TokenInfo> ListTokens(
        const std::optional<src20::TokenId>& start = std::nullopt,
        size_t count = 100) const;

    // ----- Balance Management -----

    /**
     * Get token balance for an address
     */
    uint64_t GetBalance(const CScript& address, const src20::TokenId& token_id) const;

    /**
     * Get all token balances for an address
     */
    std::vector<TokenBalance> GetAddressBalances(const CScript& address) const;

    /**
     * Get all holders of a token
     * 
     * @param token_id Token to query
     * @param min_balance Minimum balance to include (optional)
     * @param count Maximum number of holders to return
     */
    std::vector<TokenBalance> GetTokenHolders(
        const src20::TokenId& token_id,
        uint64_t min_balance = 0,
        size_t count = 100) const;

    /**
     * Transfer tokens between addresses
     * 
     * @param token_id Token to transfer
     * @param from Source address
     * @param to Destination address
     * @param amount Amount to transfer
     * @param txid Transaction ID
     * @param height Block height
     * @param time Block time
     * @return true if successful
     */
    bool TransferTokens(
        const src20::TokenId& token_id,
        const CScript& from,
        const CScript& to,
        uint64_t amount,
        const uint256& txid,
        int height,
        int64_t time,
        CDBBatch* external_batch = nullptr,
        uint16_t op_index = 0,
        BalanceOverlay* overlay = nullptr) EXCLUSIVE_LOCKS_REQUIRED(!m_cs);

    /**
     * Burn tokens from an address
     * 
     * @param token_id Token to burn
     * @param from Address burning tokens
     * @param amount Amount to burn
     * @param txid Transaction ID
     * @param height Block height
     * @param time Block time
     * @return true if successful
     */
    bool BurnTokens(
        const src20::TokenId& token_id,
        const CScript& from,
        uint64_t amount,
        const uint256& txid,
        int height,
        int64_t time,
        CDBBatch* external_batch = nullptr,
        uint16_t op_index = 0,
        BalanceOverlay* overlay = nullptr) EXCLUSIVE_LOCKS_REQUIRED(!m_cs);

    // ----- History -----

    /**
     * Get transfer history for an address
     * 
     * @param address Address to query
     * @param token_id Optional token filter
     * @param start_height Optional starting height
     * @param count Maximum records to return
     */
    std::vector<TokenTransferRecord> GetAddressHistory(
        const CScript& address,
        const std::optional<src20::TokenId>& token_id = std::nullopt,
        int start_height = 0,
        size_t count = 100) const;

    /**
     * Get transfer history for a token
     */
    std::vector<TokenTransferRecord> GetTokenHistory(
        const src20::TokenId& token_id,
        int start_height = 0,
        size_t count = 100) const;

    // ----- Block Processing -----

    /**
     * Process a block for token operations (using CBlockUndo for sender addresses)
     * 
     * @param block The block to process
     * @param height Block height
     * @param blockundo Block undo data containing spent outputs (for sender identification)
     * @return Number of token operations processed
     */
    int ProcessBlock(const CBlock& block, int height, const CBlockUndo& blockundo) EXCLUSIVE_LOCKS_REQUIRED(!m_cs);

    /**
     * Process a block for token operations (legacy version without undo data)
     * Note: This version cannot determine sender addresses for transfers/burns
     * 
     * @param block The block to process
     * @param height Block height
     * @param view Coins view for looking up spent UTXOs (optional, for backwards compatibility)
     * @return Number of token operations processed
     */
    int ProcessBlock(const CBlock& block, int height, const CCoinsViewCache* view = nullptr) EXCLUSIVE_LOCKS_REQUIRED(!m_cs);

    /**
     * Disconnect a block (for reorg handling)
     * 
     * @param block The block to disconnect
     * @param height Block height
     * @return true if successful
     */
    bool DisconnectBlock(const CBlock& block, int height) EXCLUSIVE_LOCKS_REQUIRED(!m_cs);

    /**
     * Get the best block that has been indexed
     */
    uint256 GetBestBlock() const;

    /**
     * Set the best indexed block
     */
    bool SetBestBlock(const uint256& block_hash);

    // ----- Statistics -----

    /**
     * Get total number of registered tokens
     */
    uint64_t GetTokenCount() const;

    /**
     * Get total number of token transfers
     */
    uint64_t GetTransferCount() const;

    // ----- Maintenance -----

    /**
     * Flush all pending writes to disk
     */
    bool Sync();

    /**
     * Compact the database
     */
    void Compact();
};

/**
 * Global token database instance
 */
extern std::unique_ptr<TokenDB> g_tokendb;

/**
 * Initialize the global token database
 * 
 * @param path Path to data directory
 * @param cache_size_mb Cache size in megabytes
 * @return true if successful
 */
bool InitTokenDB(const fs::path& path, size_t cache_size_mb = 64);

/**
 * Shutdown and cleanup the global token database
 */
void ShutdownTokenDB();

} // namespace tokens

#endif // OPENSY_TOKENS_TOKENDB_H
