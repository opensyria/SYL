// Copyright (c) 2024-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <tokens/tokendb.h>

#include <consensus/amount.h>
#include <hash.h>
#include <logging.h>
#include <primitives/transaction.h>
#include <script/src20.h>
#include <streams.h>
#include <util/fs.h>
#include <util/overflow.h>

namespace tokens {

/** AUDIT FIX [H-04]: Minimum transaction fee required for token issuance.
 *  Prevents token-spam attacks by requiring an economic cost to create tokens.
 *
 *  AUDIT NOTE [M-R3]: This fee is enforced in the overlay only, not at the
 *  consensus level (CheckBlock/ContextualCheckBlock). A miner who crafts a
 *  custom block can include a low-fee issuance tx. However, the practical
 *  risk is mitigated because:
 *    1. The overlay rejects it on ALL nodes (including the miner's own node),
 *       so the token is never registered in any TokenDB.
 *    2. Coinbase issuance is explicitly blocked (M-03).
 *    3. The no-UTXO-view path is explicitly blocked (M-04).
 *  Moving this check into consensus would require a hard fork and is planned
 *  for Phase 2 (deterministic token validation). See doc/src20-spec.md.
 *
 *  AUDIT FIX [R18-01]: Removed local duplicate — now uses src20::MIN_TOKEN_ISSUANCE_FEE
 *  from script/src20.h to prevent divergence. */

std::unique_ptr<TokenDB> g_tokendb;

// Database key structs for serialization
namespace {

struct TokenInfoKey {
    uint8_t prefix{db_prefix::TOKEN_INFO};
    src20::TokenId token_id;
    explicit TokenInfoKey(const src20::TokenId& id) : token_id(id) {}
    SERIALIZE_METHODS(TokenInfoKey, obj) { READWRITE(obj.prefix, obj.token_id); }
};

struct TickerKey {
    uint8_t prefix{db_prefix::TOKEN_TICKER};
    std::string ticker;
    explicit TickerKey(const std::string& t) : ticker(t) {}
    SERIALIZE_METHODS(TickerKey, obj) { READWRITE(obj.prefix, obj.ticker); }
};

struct BalanceKey {
    uint8_t prefix{db_prefix::BALANCE};
    CScript address;
    src20::TokenId token_id;
    BalanceKey(const CScript& addr, const src20::TokenId& id) : address(addr), token_id(id) {}
    SERIALIZE_METHODS(BalanceKey, obj) { READWRITE(obj.prefix, obj.address, obj.token_id); }
};

struct AddrTokensKey {
    uint8_t prefix{db_prefix::ADDR_TOKENS};
    CScript address;
    explicit AddrTokensKey(const CScript& addr) : address(addr) {}
    SERIALIZE_METHODS(AddrTokensKey, obj) { READWRITE(obj.prefix, obj.address); }
};

struct TokenHoldersKey {
    uint8_t prefix{db_prefix::TOKEN_HOLDERS};
    src20::TokenId token_id;
    CScript address;
    TokenHoldersKey(const src20::TokenId& id, const CScript& addr) : token_id(id), address(addr) {}
    SERIALIZE_METHODS(TokenHoldersKey, obj) { READWRITE(obj.prefix, obj.token_id, obj.address); }
};

struct TransferKey {
    uint8_t prefix{db_prefix::TRANSFER};
    src20::TokenId token_id;
    int height{0};
    uint256 txid;
    // AUDIT FIX [L-04]: Widened from uint8_t to uint16_t to prevent wrap at 256 ops per tx.
    uint16_t op_index{0};
    TransferKey() = default;
    TransferKey(const src20::TokenId& id, int h, const uint256& tx, uint16_t idx = 0) : token_id(id), height(h), txid(tx), op_index(idx) {}
    // AUDIT FIX [R16-S01]: Use big-endian for height and op_index so that
    // LevelDB's lexicographic key comparison produces correct ascending order.
    // Previously little-endian caused height 256 to sort before height 1,
    // breaking all range-scan queries (GetTokenHistory, GetAddressHistory)
    // once the chain exceeded 255 blocks with token activity.
    template<typename Stream> void Serialize(Stream& s) const {
        ::Serialize(s, prefix);
        ::Serialize(s, token_id);
        ser_writedata32be(s, static_cast<uint32_t>(height));
        ::Serialize(s, txid);
        // AUDIT FIX [R17-05]: Use proper ser_writedata16be instead of
        // fragile manual byte-swap through ser_writedata16.
        ser_writedata16be(s, op_index);
    }
    template<typename Stream> void Unserialize(Stream& s) {
        ::Unserialize(s, prefix);
        ::Unserialize(s, token_id);
        height = static_cast<int>(ser_readdata32be(s));
        ::Unserialize(s, txid);
        op_index = ser_readdata16be(s);
    }
};

struct BlockTokensKey {
    uint8_t prefix{db_prefix::BLOCK_TOKENS};
    int height;
    explicit BlockTokensKey(int h) : height(h) {}
    // AUDIT FIX [R16-S01]: Big-endian height for correct LevelDB ordering.
    template<typename Stream> void Serialize(Stream& s) const {
        ::Serialize(s, prefix);
        ser_writedata32be(s, static_cast<uint32_t>(height));
    }
    template<typename Stream> void Unserialize(Stream& s) {
        ::Unserialize(s, prefix);
        height = static_cast<int>(ser_readdata32be(s));
    }
};

struct UndoKey {
    uint8_t prefix{db_prefix::UNDO};
    int height;
    uint256 txid;
    // AUDIT FIX [L-04]: Widened from uint8_t to uint16_t to prevent wrap at 256 ops per tx.
    uint16_t op_index{0};
    UndoKey(int h, const uint256& tx, uint16_t idx = 0) : height(h), txid(tx), op_index(idx) {}
    // AUDIT FIX [R16-S01]: Big-endian height and op_index for correct LevelDB ordering.
    template<typename Stream> void Serialize(Stream& s) const {
        ::Serialize(s, prefix);
        ser_writedata32be(s, static_cast<uint32_t>(height));
        ::Serialize(s, txid);
        // AUDIT FIX [R17-05]: Use proper ser_writedata16be.
        ser_writedata16be(s, op_index);
    }
    template<typename Stream> void Unserialize(Stream& s) {
        ::Unserialize(s, prefix);
        height = static_cast<int>(ser_readdata32be(s));
        ::Unserialize(s, txid);
        op_index = ser_readdata16be(s);
    }
};

struct BestBlockKey {
    uint8_t prefix{db_prefix::BEST_BLOCK};
    BestBlockKey() = default;
    SERIALIZE_METHODS(BestBlockKey, obj) { READWRITE(obj.prefix); }
};

/**
 * Format a TokenUndoRecord for debug logging.
 * Provides full visibility into reorg operations for debugging.
 */
std::string FormatUndoRecord(const TokenUndoRecord& undo, int height)
{
    std::string op_type_str;
    switch (undo.op_type) {
        case TokenOpType::ISSUE: op_type_str = "ISSUE"; break;
        case TokenOpType::TRANSFER: op_type_str = "TRANSFER"; break;
        case TokenOpType::BURN: op_type_str = "BURN"; break;
        default: op_type_str = "UNKNOWN"; break;
    }
    
    std::string result = strprintf("[UNDO height=%d] op=%s txid=%s token=%s",
        height, op_type_str,
        undo.txid.ToString().substr(0, 16),
        undo.token_id.ToString().substr(0, 16));
    
    switch (undo.op_type) {
        case TokenOpType::ISSUE:
            result += strprintf(" ticker=%s", undo.ticker);
            break;
        case TokenOpType::TRANSFER:
            result += strprintf(" amount=%lu from_bal=%lu->restore to_bal=%lu->restore holders=%lu",
                undo.amount, undo.prev_from_balance, undo.prev_to_balance, undo.prev_holder_count);
            break;
        case TokenOpType::BURN:
            result += strprintf(" amount=%lu bal=%lu->restore circ=%lu->restore",
                undo.amount, undo.prev_from_balance, undo.prev_circulating_supply);
            break;
        default:
            break;
    }
    
    return result;
}

} // namespace

// AUDIT FIX [C-01/C-02]: Overlay-aware helper functions for ProcessBlock.
// These read from the in-memory overlay first (capturing pending changes
// within the current block), falling back to the DB for data not yet modified.

/** Get balance from overlay first, then DB */
static uint64_t GetBalanceWithOverlay(const TokenDB& db, const BalanceOverlay& overlay,
                                       const CScript& address, const src20::TokenId& token_id)
{
    auto key = std::make_pair(std::vector<unsigned char>(address.begin(), address.end()), token_id);
    auto it = overlay.balances.find(key);
    if (it != overlay.balances.end()) {
        return it->second;
    }
    return db.GetBalance(address, token_id);
}

/** Set balance in the overlay (does NOT write to DB/batch) */
static void SetBalanceInOverlay(BalanceOverlay& overlay,
                                 const CScript& address, const src20::TokenId& token_id,
                                 uint64_t balance)
{
    auto key = std::make_pair(std::vector<unsigned char>(address.begin(), address.end()), token_id);
    overlay.balances[key] = balance;
}

/** Get token info from overlay first, then DB */
static std::optional<TokenInfo> GetTokenInfoWithOverlay(const TokenDB& db, const BalanceOverlay& overlay,
                                                         const src20::TokenId& token_id)
{
    auto it = overlay.token_infos.find(token_id);
    if (it != overlay.token_infos.end()) {
        return it->second;
    }
    return db.GetTokenInfo(token_id);
}

/** Get addr_tokens set from overlay first, then DB — for ProcessBlock only */
static std::set<src20::TokenId> GetAddrTokensWithOverlay(const CDBWrapper& dbw, const BalanceOverlay& overlay,
                                                          const CScript& address)
{
    auto addr_key = std::vector<unsigned char>(address.begin(), address.end());
    auto it = overlay.addr_tokens.find(addr_key);
    if (it != overlay.addr_tokens.end()) {
        return it->second;
    }
    // Read from DB using the same key that production code uses
    std::set<src20::TokenId> tokens;
    dbw.Read(AddrTokensKey(address), tokens);
    return tokens;
}

static void SetAddrTokensInOverlay(BalanceOverlay& overlay,
                                    const CScript& address, std::set<src20::TokenId> tokens)
{
    auto addr_key = std::vector<unsigned char>(address.begin(), address.end());
    overlay.addr_tokens[addr_key] = std::move(tokens);
}

TokenDB::TokenDB(const fs::path& path, size_t cache_size, bool memory, bool wipe)
    : m_token_cache(MAX_CACHE_SIZE)  // Initialize LRU cache with max size
{
    DBParams params;
    params.path = path / "tokens";
    params.cache_bytes = cache_size;
    params.memory_only = memory;
    params.wipe_data = wipe;
    params.obfuscate = true;

    try {
        m_db = std::make_unique<CDBWrapper>(params);
        LogPrintf("Opened token database at %s\n", fs::PathToString(params.path));
    } catch (const std::exception& e) {
        LogPrintf("Error opening token database: %s\n", e.what());
        m_db = nullptr;
    }
}

TokenDB::~TokenDB()
{
    if (m_db) {
        Sync();
    }
}

bool TokenDB::WriteTokenInfo(const TokenInfo& info)
{
    if (!m_db) return false;
    
    CDBBatch batch(*m_db);
    batch.Write(TokenInfoKey(info.token_id), info);
    
    // Also write ticker index
    batch.Write(TickerKey(info.ticker), info.token_id);
    
    m_db->WriteBatch(batch);
    UpdateCache(info);
    return true;
}

bool TokenDB::ReadTokenInfo(const src20::TokenId& token_id, TokenInfo& info) const
{
    if (!m_db) return false;
    
    // AUDIT FIX [R14-F07]: Hold m_cs across the entire read+cache-update cycle.
    // Previously the lock was released between cache miss and DB read, creating
    // a TOCTOU race where a concurrent DisconnectBlock could invalidate the cache
    // between the miss and the UpdateCache call, causing stale pre-disconnect data
    // to be re-inserted into the cache permanently (until LRU eviction).
    LOCK(m_cs);
    auto cached = m_token_cache.get(token_id);
    if (cached) {
        info = *cached;
        return true;
    }
    
    if (!m_db->Read(TokenInfoKey(token_id), info)) {
        return false;
    }
    
    // AUDIT FIX [R25-04]: Post-deserialization length validation.
    // The Bitcoin serialization framework caps strings at MAX_SIZE (33 MB) via
    // ReadCompactSize(), but ticker and name have much smaller domain limits.
    // A corrupted DB record could encode oversized strings, causing unexpected
    // memory allocation or downstream assumptions to break. Reject at read time.
    if (info.ticker.size() > src20::MAX_TICKER_LENGTH ||
        info.name.size() > src20::MAX_NAME_LENGTH) {
        LogPrintf("ERROR: TokenInfo %s has corrupted string fields (ticker=%zu, name=%zu). "
                  "Consider running with -reindex.\n",
                  token_id.ToString().substr(0, 16),
                  info.ticker.size(), info.name.size());
        return false;
    }
    
    m_token_cache.insert(token_id, info);
    return true;
}

bool TokenDB::WriteBalance(const CScript& address, const src20::TokenId& token_id, uint64_t balance)
{
    if (!m_db) return false;
    
    CDBBatch batch(*m_db);
    WriteBalanceToBatch(batch, address, token_id, balance);
    m_db->WriteBatch(batch);
    return true;
}

void TokenDB::WriteBalanceToBatch(CDBBatch& batch, const CScript& address, const src20::TokenId& token_id, uint64_t balance)
{
    // AUDIT FIX [R8-05]: Erase zero-balance keys instead of writing 0.
    // ReadBalance() already returns 0 for missing keys, so erasure is
    // semantically identical but prevents unbounded accumulation of dead
    // balance entries in LevelDB.
    if (balance > 0) {
        batch.Write(BalanceKey(address, token_id), balance);
    } else {
        batch.Erase(BalanceKey(address, token_id));
    }
    
    // Update holder index
    auto holder_key = TokenHoldersKey(token_id, address);
    if (balance > 0) {
        batch.Write(holder_key, true);
    } else {
        batch.Erase(holder_key);
    }
}

void TokenDB::WriteTokenInfoToBatch(CDBBatch& batch, const TokenInfo& info, bool update_cache)
{
    // Write token info
    auto token_key = TokenInfoKey(info.token_id);
    batch.Write(token_key, info);
    
    // Write ticker index
    auto ticker_key = TickerKey(info.ticker);
    batch.Write(ticker_key, info.token_id);
    
    // AUDIT FIX [M-01/M-02]: Only update cache when data is committed.
    // When called from ProcessBlock with external_batch, skip caching
    // until WriteBatch succeeds to prevent stale-on-crash inconsistency.
    if (update_cache) {
        UpdateCache(info);
    }
}

bool TokenDB::ReadBalance(const CScript& address, const src20::TokenId& token_id, uint64_t& balance) const
{
    if (!m_db) return false;
    
    auto key = BalanceKey(address, token_id);
    if (!m_db->Read(key, balance)) {
        balance = 0;
        return true;  // Not an error, just zero balance
    }
    
    return true;
}

void TokenDB::InvalidateCache(const src20::TokenId& token_id) const
{
    LOCK(m_cs);
    m_token_cache.erase(token_id);
}

void TokenDB::UpdateCache(const TokenInfo& info) const
{
    LOCK(m_cs);
    // LRU cache automatically handles eviction when at capacity
    // Most recently accessed/updated entries are kept, LRU entries are evicted
    m_token_cache.insert(info.token_id, info);
}

std::optional<src20::TokenId> TokenDB::RegisterToken(
    const src20::TokenIssuance& issuance,
    const uint256& txid,
    const CScript& issuer_address,
    int height,
    int64_t time,
    CDBBatch* external_batch,
    BalanceOverlay* overlay,
    uint16_t op_index)
{
    if (!m_db) return std::nullopt;
    
    // Check if ticker already exists
    if (TickerExists(issuance.ticker)) {
        LogDebug(BCLog::TOKEN, "Token ticker %s already exists\n", issuance.ticker);
        return std::nullopt;
    }
    
    // Generate token ID from transaction hash
    src20::TokenId token_id(Txid::FromUint256(txid));
    
    // DEFENSE-IN-DEPTH: Reject duplicate token_id.
    // A SHA256d collision is computationally infeasible, but guard defensively
    // in case of database corruption or implementation bugs.
    //
    // AUDIT FIX [R26-01]: Also check the BalanceOverlay for the token_id.
    // Since TokenId = txid (no op_index component), two ISSUE ops in the
    // same tx produce the same token_id. TokenExists() only checks the DB
    // and LRU cache, which haven't been committed yet for intra-block ops.
    // Without this check, the second ISSUE would overwrite the first in the
    // batch, permanently orphaning the first ticker in the ticker index.
    if (TokenExists(token_id)) {
        LogPrintf("ERROR: Token ID %s already exists (SHA256d collision or DB corruption)\n",
                  token_id.ToString().substr(0, 16));
        return std::nullopt;
    }
    if (overlay && overlay->token_infos.count(token_id)) {
        LogPrintf("ERROR: Token ID %s already registered earlier in this block (duplicate txid issuance)\n",
                  token_id.ToString().substr(0, 16));
        return std::nullopt;
    }
    
    // Create token info
    TokenInfo info;
    info.token_id = token_id;
    info.ticker = issuance.ticker;
    info.name = issuance.name;
    info.decimals = issuance.decimals;
    info.total_supply = issuance.total_supply;
    info.circulating_supply = issuance.total_supply;
    info.metadata_hash = issuance.metadata_hash;
    info.issuance_txid = txid;
    info.issuance_height = height;
    info.issuance_time = time;
    info.holder_count = 1;
    info.transfer_count = 0;
    
    // AUDIT FIX [H-03]: Use external batch when provided for atomic block processing.
    // When called from ProcessBlock, all ops + undo go into one atomic batch.
    // When called standalone (external_batch == nullptr), use local batch.
    CDBBatch local_batch(*m_db);
    CDBBatch& batch = external_batch ? *external_batch : local_batch;
    
    // Write token info
    batch.Write(TokenInfoKey(info.token_id), info);
    batch.Write(TickerKey(info.ticker), info.token_id);
    
    // Credit initial supply to issuer
    batch.Write(BalanceKey(issuer_address, token_id), issuance.total_supply);
    batch.Write(TokenHoldersKey(token_id, issuer_address), true);
    
    // Update address token list (overlay-aware for intra-block consistency)
    std::set<src20::TokenId> addr_tokens;
    if (overlay) {
        addr_tokens = GetAddrTokensWithOverlay(*m_db, *overlay, issuer_address);
    } else {
        m_db->Read(AddrTokensKey(issuer_address), addr_tokens);
    }
    addr_tokens.insert(token_id);
    batch.Write(AddrTokensKey(issuer_address), addr_tokens);
    if (overlay) {
        SetAddrTokensInOverlay(*overlay, issuer_address, addr_tokens);
        // Keep overlay balance & token_info current so subsequent
        // operations in the same block see the newly-issued token.
        SetBalanceInOverlay(*overlay, issuer_address, token_id, issuance.total_supply);
        overlay->token_infos[token_id] = info;
    }
    
    // Record issuance as transfer from empty script (marks it as an issuance)
    TokenTransferRecord record;
    record.token_id = token_id;
    record.txid = txid;
    record.from_address = CScript();  // Empty = issuance
    record.to_address = issuer_address;
    record.amount = issuance.total_supply;
    record.height = height;
    record.time = time;
    
    // AUDIT FIX [v4-ISSUE-002]: Include op_index in TransferKey so that
    // DisconnectBlock's ISSUE undo can erase the correct key.  Previously
    // op_index defaulted to 0, causing a key mismatch when the ISSUE was
    // not the first SRC-20 op in its transaction.
    batch.Write(TransferKey(token_id, height, txid, op_index), record);
    
    // Commit only if using local batch (standalone mode)
    if (!external_batch) {
        try {
            m_db->WriteBatch(local_batch);
        } catch (const dbwrapper_error& e) {
            LogPrintf("Failed to write token registration for %s: %s\n", issuance.ticker, e.what());
            return std::nullopt;
        }
    }
    
    // AUDIT FIX [M-01/M-02]: Only update cache when using local batch (data is committed).
    // When external_batch is provided, the batch hasn't been committed yet — caching now
    // would create a stale-on-crash inconsistency. ProcessBlock updates cache after WriteBatch.
    if (!external_batch) {
        UpdateCache(info);
    }
    
    // FIX [R5-03]: Only update counter after data is committed. When called
    // from ProcessBlock with external_batch, the batch hasn't been committed yet.
    // If WriteBatch later fails, incrementing now would leave the counter inflated.
    // ProcessBlock handles the post-commit counter update for the external case.
    if (!external_batch && m_token_count_initialized.load()) {
        m_token_count_cache.fetch_add(1);
    }
    
    LogDebug(BCLog::TOKEN, "Registered new token: %s (%s) with ID %s\n",
             issuance.name, issuance.ticker, token_id.ToString().substr(0, 16));
    
    return token_id;
}

std::optional<TokenInfo> TokenDB::GetTokenInfo(const src20::TokenId& token_id) const
{
    TokenInfo info;
    if (ReadTokenInfo(token_id, info)) {
        return info;
    }
    return std::nullopt;
}

std::optional<TokenInfo> TokenDB::GetTokenByTicker(const std::string& ticker) const
{
    if (!m_db) return std::nullopt;
    
    auto ticker_key = TickerKey(ticker);
    src20::TokenId token_id;
    if (!m_db->Read(ticker_key, token_id)) {
        return std::nullopt;
    }
    
    return GetTokenInfo(token_id);
}

bool TokenDB::TokenExists(const src20::TokenId& token_id) const
{
    if (!m_db) return false;
    
    // Check LRU cache first (without updating access order - use contains)
    {
        LOCK(m_cs);
        if (m_token_cache.contains(token_id)) {
            return true;
        }
    }
    
    auto key = TokenInfoKey(token_id);
    return m_db->Exists(key);
}

bool TokenDB::TickerExists(const std::string& ticker) const
{
    if (!m_db) return false;
    
    auto key = TickerKey(ticker);
    return m_db->Exists(key);
}

std::vector<TokenInfo> TokenDB::ListTokens(
    const std::optional<src20::TokenId>& start,
    size_t count) const
{
    std::vector<TokenInfo> result;
    if (!m_db) return result;
    
    std::unique_ptr<CDBIterator> cursor(m_db->NewIterator());
    
    if (start) {
        auto start_key = TokenInfoKey(*start);
        cursor->Seek(start_key);
    } else {
        cursor->Seek(db_prefix::TOKEN_INFO);
    }
    
    // AUDIT FIX [R22-01]: Add MAX_TOTAL_ITERATIONS safety cap to prevent
    // unbounded iteration on corrupt DB entries where GetKey/GetValue fail
    // repeatedly without advancing past the TOKEN_INFO prefix.
    static constexpr size_t MAX_TOTAL_ITERATIONS = 100000;
    size_t total_iterations = 0;
    while (cursor->Valid() && result.size() < count) {
        if (++total_iterations > MAX_TOTAL_ITERATIONS) break;
        TokenInfoKey key_obj{src20::TokenId{}};
        if (!cursor->GetKey(key_obj)) {
            cursor->Next();
            continue;
        }
        if (key_obj.prefix != db_prefix::TOKEN_INFO) {
            break;
        }
        
        TokenInfo info;
        if (!cursor->GetValue(info)) {
            cursor->Next();
            continue;
        }
        result.push_back(info);
        
        cursor->Next();
    }
    
    return result;
}

uint64_t TokenDB::GetBalance(const CScript& address, const src20::TokenId& token_id) const
{
    uint64_t balance = 0;
    ReadBalance(address, token_id, balance);
    return balance;
}

std::vector<TokenBalance> TokenDB::GetAddressBalances(const CScript& address, size_t max_results) const
{
    std::vector<TokenBalance> result;
    if (!m_db) return result;
    
    // Read the address token list
    auto addr_key = AddrTokensKey(address);
    std::set<src20::TokenId> token_ids;
    if (!m_db->Read(addr_key, token_ids)) {
        return result;
    }
    
    // AUDIT FIX [R11-02]: Early-exit once we have enough results to avoid
    // unbounded DB reads when an address holds thousands of tokens.
    for (const auto& token_id : token_ids) {
        if (max_results > 0 && result.size() >= max_results) break;
        uint64_t balance = GetBalance(address, token_id);
        if (balance > 0) {
            result.emplace_back(token_id, address, balance);
        }
    }
    
    return result;
}

std::vector<TokenBalance> TokenDB::GetTokenHolders(
    const src20::TokenId& token_id,
    uint64_t min_balance,
    size_t count) const
{
    std::vector<TokenBalance> result;
    if (!m_db) return result;
    
    std::unique_ptr<CDBIterator> cursor(m_db->NewIterator());
    
    auto start_key = TokenHoldersKey(token_id, CScript{});
    cursor->Seek(start_key);
    
    // AUDIT FIX [R11-01]: Cap total iterations to prevent DoS when
    // min_balance filters out most holders, causing an unbounded scan
    // of potentially millions of holder entries.
    static constexpr size_t MAX_TOTAL_ITERATIONS = 100000;
    size_t total_iterations = 0;
    
    while (cursor->Valid() && result.size() < count) {
        if (++total_iterations > MAX_TOTAL_ITERATIONS) break;
        
        TokenHoldersKey key_obj{src20::TokenId{}, CScript{}};
        if (!cursor->GetKey(key_obj)) {
            cursor->Next();
            continue;
        }
        
        // Check if still in the same token's holders
        if (key_obj.prefix != db_prefix::TOKEN_HOLDERS || key_obj.token_id != token_id) {
            break;
        }
        
        // We only store a marker value (true), balance is stored separately
        uint64_t balance = GetBalance(key_obj.address, token_id);
        if (balance >= min_balance) {
            result.emplace_back(token_id, key_obj.address, balance);
        }
        
        cursor->Next();
    }
    
    return result;
}

bool TokenDB::TransferTokens(
    const src20::TokenId& token_id,
    const CScript& from,
    const CScript& to,
    uint64_t amount,
    const uint256& txid,
    int height,
    int64_t time,
    CDBBatch* external_batch,
    uint16_t op_index,
    BalanceOverlay* overlay)
{
    if (!m_db) return false;

    // DEFENSE-IN-DEPTH [R7-D2]: Reject zero-amount transfers.
    // Callers gate on TokenTransfer::IsValid() which rejects amount==0, but a
    // future direct caller could bypass that.  A zero-amount transfer would
    // write a pointless TransferRecord and — if to_balance==0 — incorrectly
    // increment holder_count for an address with no actual tokens.
    if (amount == 0) {
        LogDebug(BCLog::TOKEN, "TransferTokens rejected: amount is zero\n");
        return false;
    }

    // SECURITY FIX [C-03]: Self-transfer guard.
    // When from == to, the second batch.Write(BalanceKey(to, ...), to_balance + amount)
    // overwrites the first batch.Write(BalanceKey(from, ...), from_balance - amount),
    // inflating the sender's balance by +amount (token creation from thin air).
    // A self-transfer is a no-op balance-wise, so we still record the transfer
    // for history and update stats, but skip all balance modifications.
    if (from == to) {
        // Validate sender has sufficient balance
        uint64_t from_balance = overlay ? GetBalanceWithOverlay(*this, *overlay, from, token_id)
                                        : GetBalance(from, token_id);
        if (from_balance < amount) {
            LogDebug(BCLog::TOKEN, "Insufficient token balance for self-transfer: have %llu, need %llu\n",
                     from_balance, amount);
            return false;
        }

        CDBBatch local_batch(*m_db);
        CDBBatch& batch = external_batch ? *external_batch : local_batch;

        // Record transfer for history (balance unchanged — no writes to BalanceKey)
        TokenTransferRecord record;
        record.token_id = token_id;
        record.txid = txid;
        record.from_address = from;
        record.to_address = to;
        record.amount = amount;
        record.height = height;
        record.time = time;

        auto transfer_key = TransferKey(token_id, height, txid, op_index);
        batch.Write(transfer_key, record);

        // Update transfer count in token info
        TokenInfo info;
        bool found_info = false;
        if (overlay) {
            auto oi = overlay->token_infos.find(token_id);
            if (oi != overlay->token_infos.end()) {
                info = oi->second;
                found_info = true;
            }
        }
        if (!found_info) {
            found_info = ReadTokenInfo(token_id, info);
        }
        if (found_info) {
            info.transfer_count++;
            batch.Write(TokenInfoKey(info.token_id), info);
            // AUDIT FIX [R13-04]: Removed redundant TickerKey write.
            // The ticker→token_id mapping is immutable after RegisterToken;
            // re-writing it on every self-transfer is wasted I/O.
            if (overlay) {
                overlay->token_infos[token_id] = info;
            }
            if (!external_batch) {
                UpdateCache(info);
            }
        }

        if (!external_batch) {
            m_db->WriteBatch(local_batch);
        }

        // FIX [R5-03]: Only update counter after data is committed (see RegisterToken).
        if (!external_batch && m_transfer_count_initialized.load()) {
            m_transfer_count_cache.fetch_add(1);
        }

        LogDebug(BCLog::TOKEN, "Token self-transfer (no-op): %llu of %s\n",
                 amount, token_id.ToString().substr(0, 16));
        return true;
    }

    // AUDIT FIX [C-01/C-02]: Use overlay-aware reads when processing a block.
    // Without this, two transfers from the same address in one block each read
    // the original DB balance, enabling double-spend (token inflation).
    uint64_t from_balance = overlay ? GetBalanceWithOverlay(*this, *overlay, from, token_id)
                                    : GetBalance(from, token_id);
    if (from_balance < amount) {
        LogDebug(BCLog::TOKEN, "Insufficient token balance: have %llu, need %llu\n",
                 from_balance, amount);
        return false;
    }
    
    // Update balances
    uint64_t to_balance = overlay ? GetBalanceWithOverlay(*this, *overlay, to, token_id)
                                  : GetBalance(to, token_id);
    
    // SECURITY: Check for overflow before adding to recipient balance
    auto new_to_balance = CheckedAdd(to_balance, amount);
    if (!new_to_balance) {
        LogDebug(BCLog::TOKEN, "Token transfer would cause balance overflow: %llu + %llu\n",
                 to_balance, amount);
        return false;
    }
    
    // AUDIT FIX [H-03]: Use external batch when provided for atomic block processing.
    CDBBatch local_batch(*m_db);
    CDBBatch& batch = external_batch ? *external_batch : local_batch;
    
    // Write sender's new balance
    // AUDIT FIX [R8-06]: Erase the key when balance reaches zero instead of
    // writing 0, preventing unbounded dead-key accumulation.  ReadBalance()
    // already returns 0 for missing keys.
    uint64_t new_from = from_balance - amount;
    if (new_from > 0) {
        batch.Write(BalanceKey(from, token_id), new_from);
    } else {
        batch.Erase(BalanceKey(from, token_id));
    }
    
    // AUDIT FIX [C-01/C-02]: Update overlay so subsequent ops in the same block
    // see the correct post-transfer balances instead of stale DB values.
    if (overlay) {
        SetBalanceInOverlay(*overlay, from, token_id, new_from);
    }
    
    // Update sender's holder index
    auto from_holder_key = TokenHoldersKey(token_id, from);
    if (from_balance == amount) {
        // Sender will have zero balance - remove from holder index
        batch.Erase(from_holder_key);
        
        // AUDIT FIX [ISSUE-017]: Remove token from sender's addr_tokens set
        // when their balance reaches zero.  Without this, the address retains
        // a stale entry in its token list, showing tokens it no longer holds.
        auto from_addr_key = AddrTokensKey(from);
        std::set<src20::TokenId> from_tokens;
        if (overlay) {
            from_tokens = GetAddrTokensWithOverlay(*m_db, *overlay, from);
        } else {
            m_db->Read(from_addr_key, from_tokens);
        }
        from_tokens.erase(token_id);
        if (from_tokens.empty()) {
            batch.Erase(from_addr_key);
        } else {
            batch.Write(from_addr_key, from_tokens);
        }
        if (overlay) {
            SetAddrTokensInOverlay(*overlay, from, from_tokens);
        }
    }
    
    // Write recipient's new balance
    batch.Write(BalanceKey(to, token_id), *new_to_balance);
    
    // AUDIT FIX [C-01/C-02]: Update overlay for recipient balance.
    if (overlay) {
        SetBalanceInOverlay(*overlay, to, token_id, *new_to_balance);
    }
    
    // Update recipient's holder index
    auto to_holder_key = TokenHoldersKey(token_id, to);
    batch.Write(to_holder_key, true);
    
    // Add token to recipient's list if new holder
    if (to_balance == 0) {
        auto to_addr_key = AddrTokensKey(to);
        std::set<src20::TokenId> to_tokens;
        if (overlay) {
            to_tokens = GetAddrTokensWithOverlay(*m_db, *overlay, to);
        } else {
            m_db->Read(to_addr_key, to_tokens);
        }
        to_tokens.insert(token_id);
        batch.Write(to_addr_key, to_tokens);
        if (overlay) {
            SetAddrTokensInOverlay(*overlay, to, to_tokens);
        }
    }
    
    // Record transfer
    TokenTransferRecord record;
    record.token_id = token_id;
    record.txid = txid;
    record.from_address = from;
    record.to_address = to;
    record.amount = amount;
    record.height = height;
    record.time = time;
    
    // SECURITY FIX [M-19]: Pass op_index to TransferKey to prevent history
    // overwrites when multiple transfers of the same token occur in one tx.
    // Previously the default op_index=0 caused all same-tx transfers to
    // collide on the same key, losing all but the last transfer record.
    auto transfer_key = TransferKey(token_id, height, txid, op_index);
    batch.Write(transfer_key, record);
    
    // Include token stats update in the same batch
    TokenInfo info;
    bool found_info = false;
    // AUDIT FIX [C-01/C-02]: Read token info from overlay if available
    if (overlay) {
        auto oi = overlay->token_infos.find(token_id);
        if (oi != overlay->token_infos.end()) {
            info = oi->second;
            found_info = true;
        }
    }
    if (!found_info) {
        found_info = ReadTokenInfo(token_id, info);
    }
    if (found_info) {
        info.transfer_count++;
        
        // Update holder count
        if (to_balance == 0) info.holder_count++;
        // FIX [R5-02]: Guard against holder_count underflow (matches BurnTokens).
        // If holder_count is already 0 (e.g. DB corruption or prior bug),
        // decrementing a uint64_t wraps to UINT64_MAX.
        if (from_balance == amount) {
            if (info.holder_count > 0) {
                info.holder_count--;
            } else {
                LogPrintf("WARNING: Token %s holder_count already 0 during transfer from %s\n",
                          token_id.ToString().substr(0, 16),
                          HexStr(std::vector<unsigned char>(from.begin(), from.end())).substr(0, 16));
            }
        }
        
        // Write to batch instead of separate WriteTokenInfo call
        batch.Write(TokenInfoKey(info.token_id), info);
        // AUDIT FIX [R13-04]: Removed redundant TickerKey write.
        // The ticker→token_id mapping is immutable after RegisterToken.
        // AUDIT FIX [C-01/C-02]: Update overlay token info so subsequent ops
        // in the same block see correct holder_count/transfer_count.
        if (overlay) {
            overlay->token_infos[token_id] = info;
        }
        // AUDIT FIX [M-01/M-02]: Only update cache when data is committed.
        if (!external_batch) {
            UpdateCache(info);
        }
    }
    
    // Commit only if using local batch (standalone mode)
    if (!external_batch) {
        m_db->WriteBatch(local_batch);
    }
    
    // FIX [R5-03]: Only update counter after data is committed (see RegisterToken).
    if (!external_batch && m_transfer_count_initialized.load()) {
        m_transfer_count_cache.fetch_add(1);
    }
    
    LogDebug(BCLog::TOKEN, "Token transfer: %llu of %s\n", amount, token_id.ToString().substr(0, 16));
    
    return true;
}

bool TokenDB::BurnTokens(
    const src20::TokenId& token_id,
    const CScript& from,
    uint64_t amount,
    const uint256& txid,
    int height,
    int64_t time,
    CDBBatch* external_batch,
    uint16_t op_index,
    BalanceOverlay* overlay)
{
    if (!m_db) return false;
    
    // AUDIT FIX [C-01/C-02]: Use overlay-aware reads when processing a block.
    uint64_t from_balance = overlay ? GetBalanceWithOverlay(*this, *overlay, from, token_id)
                                    : GetBalance(from, token_id);
    if (from_balance < amount) {
        LogDebug(BCLog::TOKEN, "Insufficient token balance for burn: have %llu, need %llu\n",
                 from_balance, amount);
        return false;
    }
    
    // Read token info for validation before modifying anything
    TokenInfo info;
    bool found_info = false;
    // AUDIT FIX [C-01/C-02]: Read token info from overlay if available
    if (overlay) {
        auto oi = overlay->token_infos.find(token_id);
        if (oi != overlay->token_infos.end()) {
            info = oi->second;
            found_info = true;
        }
    }
    if (!found_info) {
        found_info = ReadTokenInfo(token_id, info);
    }
    if (!found_info) {
        LogDebug(BCLog::TOKEN, "Token burn failed: token %s not found\n",
                 token_id.ToString().substr(0, 16));
        return false;
    }
    
    // SECURITY FIX [M-01]: Check for circulating_supply underflow before modifying
    if (info.circulating_supply < amount) {
        LogPrintf("ERROR: Token burn would cause circulating supply underflow for %s: supply=%llu, burn=%llu\n",
                  token_id.ToString().substr(0, 16), info.circulating_supply, amount);
        return false;
    }
    
    // AUDIT FIX [H-03]: Use external batch when provided for atomic block processing.
    CDBBatch local_batch(*m_db);
    CDBBatch& batch = external_batch ? *external_batch : local_batch;
    
    // Write sender's new balance
    uint64_t new_from = from_balance - amount;
    WriteBalanceToBatch(batch, from, token_id, new_from);
    
    // AUDIT FIX [C-01/C-02]: Update overlay so subsequent ops in the same block
    // see the correct post-burn balance instead of stale DB values.
    if (overlay) {
        SetBalanceInOverlay(*overlay, from, token_id, new_from);
    }
    
    // Update holder index if sender now has zero balance
    // NOTE: WriteBalanceToBatch above already erased the TokenHoldersKey
    // (R8-05 erase-on-zero path), so we only need to handle holder_count
    // and addr_tokens cleanup here.
    if (from_balance == amount) {
        if (info.holder_count > 0) {
            info.holder_count--;
        }
        
        // AUDIT FIX [ISSUE-017]: Remove token from burner's addr_tokens set
        // when their balance reaches zero (mirrors TransferTokens fix).
        auto from_addr_key = AddrTokensKey(from);
        std::set<src20::TokenId> from_tokens;
        if (overlay) {
            from_tokens = GetAddrTokensWithOverlay(*m_db, *overlay, from);
        } else {
            m_db->Read(from_addr_key, from_tokens);
        }
        from_tokens.erase(token_id);
        if (from_tokens.empty()) {
            batch.Erase(from_addr_key);
        } else {
            batch.Write(from_addr_key, from_tokens);
        }
        if (overlay) {
            SetAddrTokensInOverlay(*overlay, from, from_tokens);
        }
    }
    
    // Update circulating supply (already checked for underflow above)
    info.circulating_supply -= amount;
    
    // Write token info to batch (atomic with balance update)
    // AUDIT FIX [M-01/M-02]: Skip cache update when using external_batch.
    WriteTokenInfoToBatch(batch, info, /*update_cache=*/!external_batch);
    // AUDIT FIX [C-01/C-02]: Update overlay token info
    if (overlay) {
        overlay->token_infos[token_id] = info;
    }
    
    // Record burn as transfer to empty script (marks it as a burn for history)
    TokenTransferRecord record;
    record.token_id = token_id;
    record.txid = txid;
    record.from_address = from;
    record.to_address = CScript();  // Empty = burn
    record.amount = amount;
    record.height = height;
    record.time = time;
    
    // SECURITY FIX [M-19]: Pass op_index to TransferKey to prevent history
    // overwrites when multiple burns of the same token occur in one tx.
    auto transfer_key = TransferKey(token_id, height, txid, op_index);
    batch.Write(transfer_key, record);
    
    // Commit only if using local batch (standalone mode)
    if (!external_batch) {
        m_db->WriteBatch(local_batch);
    }
    
    // FIX [R5-03]: Only update counter after data is committed (see RegisterToken).
    if (!external_batch && m_transfer_count_initialized.load()) {
        m_transfer_count_cache.fetch_add(1);
    }
    
    LogDebug(BCLog::TOKEN, "Token burn: %llu of %s (remaining supply: %llu)\n",
             amount, token_id.ToString().substr(0, 16), info.circulating_supply);
    
    return true;
}

std::vector<TokenTransferRecord> TokenDB::GetAddressHistory(
    const CScript& address,
    const std::optional<src20::TokenId>& token_id,
    int start_height,
    size_t count) const
{
    std::vector<TokenTransferRecord> result;
    if (!m_db) return result;
    
    std::set<src20::TokenId> search_tokens;
    
    if (token_id) {
        search_tokens.insert(*token_id);
    } else {
        // NOTE: addr_tokens tracks tokens the address CURRENTLY holds (tokens are
        // removed when balance reaches 0 via ISSUE-017).  This means history for
        // fully-divested tokens is omitted.  For complete history of a specific
        // token, callers should pass the token_id parameter explicitly.
        auto addr_key = AddrTokensKey(address);
        m_db->Read(addr_key, search_tokens);
    }
    
    // Search transfer records for each token
    static constexpr size_t MAX_TOTAL_ITERATIONS = 100000;
    size_t total_iterations = 0;
    for (const auto& tid : search_tokens) {
        std::unique_ptr<CDBIterator> cursor(m_db->NewIterator());
        
        // FIX: Seek to start_height instead of 0 to avoid wasting the
        // iteration budget scanning irrelevant records below the requested
        // height. Previously, tokens with >100K transfers before start_height
        // would exhaust MAX_TOTAL_ITERATIONS and return empty/truncated results.
        auto start_key = TransferKey(tid, start_height, uint256{});
        cursor->Seek(start_key);
        
        while (cursor->Valid() && result.size() < count) {
            if (++total_iterations > MAX_TOTAL_ITERATIONS) break;
            TransferKey key_obj;
            if (!cursor->GetKey(key_obj)) {
                cursor->Next();
                continue;
            }
            if (key_obj.prefix != db_prefix::TRANSFER || key_obj.token_id != tid) {
                break;
            }
            
            TokenTransferRecord record;
            if (!cursor->GetValue(record)) {
                cursor->Next();
                continue;
            }
            
            // Check if this address is involved
            if (record.from_address == address || record.to_address == address) {
                if (record.height >= start_height) {
                    result.push_back(record);
                }
            }
            
            cursor->Next();
        }
        if (total_iterations > MAX_TOTAL_ITERATIONS) break;
    }
    
    // Sort by height descending
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.height > b.height;
    });
    
    if (result.size() > count) {
        result.resize(count);
    }
    
    return result;
}

std::vector<TokenTransferRecord> TokenDB::GetTokenHistory(
    const src20::TokenId& token_id,
    int start_height,
    size_t count) const
{
    std::vector<TokenTransferRecord> result;
    if (!m_db) return result;
    
    std::unique_ptr<CDBIterator> cursor(m_db->NewIterator());
    
    // FIX: Seek directly to start_height to avoid scanning all records
    // below the requested height. Same fix as GetAddressHistory.
    auto start_key = TransferKey(token_id, start_height, uint256{});
    cursor->Seek(start_key);
    
    // AUDIT FIX [R17-03]: Add MAX_TOTAL_ITERATIONS safety cap, matching
    // GetAddressHistory.  Without this, a corrupt or unparseable DB record
    // (where GetKey/GetValue fail and cursor->Next() is called without
    // incrementing result.size()) could spin indefinitely through the
    // entire transfer prefix, hanging the RPC thread.
    static constexpr size_t MAX_TOTAL_ITERATIONS = 100000;
    size_t total_iterations = 0;
    while (cursor->Valid() && result.size() < count) {
        if (++total_iterations > MAX_TOTAL_ITERATIONS) break;
        TransferKey key_obj;
        if (!cursor->GetKey(key_obj)) {
            cursor->Next();
            continue;
        }
        if (key_obj.prefix != db_prefix::TRANSFER || key_obj.token_id != token_id) {
            break;
        }
        
        TokenTransferRecord record;
        if (!cursor->GetValue(record)) {
            cursor->Next();
            continue;
        }
        
        if (record.height >= start_height) {
            result.push_back(record);
        }
        
        cursor->Next();
    }
    
    return result;
}

int TokenDB::ProcessBlock(const CBlock& block, int height, const CBlockUndo& blockundo)
{
    if (!m_db) return 0;

    // CRITICAL FIX [R5-01]: Guard against reprocessing after crash.
    // Token ProcessBlock commits directly to LevelDB (WriteBatch at end of
    // this function), but the UTXO coins view is flushed later in ConnectTip.
    // If the node crashes between these two points, on restart the chain
    // replays ConnectBlock for this height, calling ProcessBlock again.
    // Without this guard, transfers and burns would be applied twice:
    //   - Double-transfer: recipient gets 2x tokens (token inflation)
    //   - Double-burn: circulating supply reduced by 2x (excess destruction)
    // The BestBlock marker is written atomically with all token changes,
    // so if it already matches this block's hash, the block was fully processed.
    {
        uint256 best_block = GetBestBlock();
        if (best_block == block.GetHash()) {
            LogPrintf("Token ProcessBlock: block %d already processed (BestBlock matches %s), skipping reprocess\n",
                      height, best_block.ToString().substr(0, 16));
            return 0;
        }
    }

    int ops_count = 0;
    // FIX [R5-03]: Track per-type counts for post-commit counter updates.
    int issue_count = 0;
    int txfr_count = 0;
    int64_t block_time = block.GetBlockTime();
    std::vector<uint256> block_token_txs;
    CDBBatch batch(*m_db);

    // AUDIT FIX [C-01/C-02]: In-memory balance overlay prevents stale reads.
    // Without this, two transfers from the same address in one block each read
    // the original balance from DB, enabling double-spend (token inflation).
    // The overlay tracks cumulative balance changes across all ops in the block.
    BalanceOverlay overlay;

    // AUDIT FIX [M-04]: Enforce MAX_TOKENS_PER_BLOCK at the processing level.
    // Previously this limit was only checked in mempool relay policy, allowing
    // miners to include unlimited token ops and cause I/O load on all nodes.
    static constexpr size_t MAX_OPS = src20::MAX_TOKENS_PER_BLOCK;

    // AUDIT FIX [H-05]: Per-transaction SRC-20 operation limit is enforced in
    // ParseTransactionSRC20() (MAX_OPS_PER_TX = 4), which caps the ops vector
    // size before it reaches this loop.
    
    // blockundo.vtxundo has entries for non-coinbase transactions (index i-1 for block.vtx[i])
    for (unsigned int i = 0; i < block.vtx.size() && static_cast<size_t>(ops_count) < MAX_OPS; i++) {
        const auto& tx = block.vtx[i];
        auto ops = src20::ParseTransactionSRC20(*tx);
        
        // DEFENSE-IN-DEPTH [R4-02]: Skip coinbase and guard against consensus-
        // invalid blocks where i==0 is not a coinbase.  Without this, unsigned
        // `i - 1` would wrap to UINT_MAX, causing out-of-bounds UB in vtxundo.
        if (tx->IsCoinBase() || i == 0) {
            if (!ops.empty()) {
                LogDebug(BCLog::TOKEN, "Skipping token ops in coinbase/index-0 tx\n");
            }
            continue;
        }

        // Derive sender from first input's spent UTXO using blockundo
        CScript tx_sender;
        if (!tx->vin.empty()) {
            // AUDIT FIX [R26-02]: Bounds check before vtxundo access.
            // In normal operation, vtxundo.size() == vtx.size() - 1, but if
            // ReadBlockUndo produced truncated data (e.g., during startup
            // reconciliation replay on partially-corrupt disk), this prevents
            // undefined behavior from out-of-bounds access.
            if (static_cast<size_t>(i - 1) >= blockundo.vtxundo.size()) {
                LogPrintf("ERROR: ProcessBlock: vtxundo size mismatch at tx index %u "
                          "(vtxundo.size()=%zu, expected >= %u). Skipping tx.\n",
                          i, blockundo.vtxundo.size(), i);
                continue;
            }
            // blockundo.vtxundo[i-1] contains undo for tx at block.vtx[i]
            const CTxUndo& txundo = blockundo.vtxundo[i - 1];
            // vprevout[0] is the first spent output (for vin[0])
            if (!txundo.vprevout.empty()) {
                tx_sender = txundo.vprevout[0].out.scriptPubKey;
            }
        }
        
    // SECURITY FIX [C-02]: Track per-tx operation index to prevent undo key collision.
        // Each op within the same tx gets a unique op_index for its UndoKey and TransferKey.
        // AUDIT FIX [L-04]: Widened from uint8_t to uint16_t to prevent wrap at 256 ops per tx.
        uint16_t tx_op_index = 0;
        // AUDIT FIX [R14-F01]: Track issuance count per-TX so the fee check
        // scales linearly.  Previously a single 100 SYL fee was shared across
        // all ISSUE ops in the same tx, allowing up to 4 tokens for the price
        // of one.  Now the Nth issuance requires N × MIN_TOKEN_ISSUANCE_FEE.
        int issue_count_this_tx = 0;

        for (const auto& op : ops) {
            // AUDIT FIX [M-04]: Stop processing once block limit reached
            if (static_cast<size_t>(ops_count) >= MAX_OPS) {
                LogDebug(BCLog::TOKEN, "MAX_TOKENS_PER_BLOCK (%zu) reached at height %d, skipping remaining ops\n", MAX_OPS, height);
                break;
            }
            switch (op.action) {
                case src20::TokenAction::ISSUE: {
                    const auto* issuance = op.GetIssuance();
                    if (issuance && issuance->IsValid()) {
                        // AUDIT FIX [H-04]: Enforce minimum issuance fee to prevent token spam
                        // AUDIT FIX [M-03]: Coinbase transactions must also pay the fee.
                        // Previously coinbase was exempt, allowing miners to issue tokens for free.
                        if (i > 0 && !tx->IsCoinBase()) {
                            const CTxUndo& fee_undo = blockundo.vtxundo[i - 1];
                            CAmount total_in = 0;
                            for (const auto& prev : fee_undo.vprevout) {
                                total_in += prev.out.nValue;
                            }
                            CAmount total_out = 0;
                            for (const auto& out : tx->vout) {
                                total_out += out.nValue;
                            }
                            CAmount tx_fee = total_in - total_out;
                            // AUDIT FIX [R14-F01]: Require fee proportional to issuance count.
                            CAmount required_fee = src20::MIN_TOKEN_ISSUANCE_FEE * (issue_count_this_tx + 1);
                            if (tx_fee < required_fee) {
                                LogDebug(BCLog::TOKEN, "Token issuance rejected: fee %lld < minimum %lld (issuance #%d in tx)\n",
                                         tx_fee, required_fee, issue_count_this_tx + 1);
                                break;
                            }
                        } else if (tx->IsCoinBase()) {
                            // AUDIT FIX [M-03]: Coinbase token issuance is not allowed.
                            // Miners could previously issue tokens for free by placing
                            // SRC-20 ISSUE ops in the coinbase, bypassing the anti-spam fee.
                            LogDebug(BCLog::TOKEN, "Token issuance rejected: coinbase transactions cannot issue tokens\n");
                            break;
                        }
                        
                        // Get issuer address from first non-OP_RETURN output
                        CScript issuer;
                        for (const auto& vout : tx->vout) {
                            if (!vout.scriptPubKey.IsUnspendable()) {
                                issuer = vout.scriptPubKey;
                                break;
                            }
                        }

                        // DEFENSE-IN-DEPTH [R7-D1]: Reject issuance with no
                        // spendable outputs. All outputs being OP_RETURN means
                        // the entire supply is credited to an empty CScript,
                        // making the tokens permanently unreachable while
                        // showing a misleading holder_count of 1.
                        if (issuer.empty()) {
                            LogDebug(BCLog::TOKEN, "Token issuance rejected: no spendable output for issuer address\n");
                            break;
                        }

                        // AUDIT FIX [ISSUE-001]: Reject duplicate tickers within the same block.
                        // The DB-level TickerExists() check only sees committed data, so two
                        // ISSUE ops for the same ticker in one block would both pass.  The
                        // overlay's pending_tickers set catches the intra-block duplicate.
                        if (overlay.pending_tickers.count(issuance->ticker)) {
                            LogPrintf("Token issuance rejected: duplicate ticker '%s' within block at height %d\n",
                                      issuance->ticker, height);
                            break;
                        }
                        
                        const uint256 txhash = tx->GetHash().ToUint256();
                        // AUDIT FIX [v4-ISSUE-002]: Pass tx_op_index so RegisterToken writes
                        // the TransferRecord at the correct key for DisconnectBlock erasure.
                        auto token_id_opt = RegisterToken(*issuance, txhash, issuer, height, block_time, &batch, &overlay, tx_op_index);
                        if (token_id_opt) {
                            overlay.pending_tickers.insert(issuance->ticker);
                            // Create undo record for token issuance
                            TokenUndoRecord undo;
                            undo.op_type = TokenOpType::ISSUE;
                            undo.txid = txhash;
                            undo.token_id = *token_id_opt;
                            undo.ticker = issuance->ticker;
                            // AUDIT FIX [H-01]: Store issuer address for reorg cleanup.
                            // Previously from_address was left empty, causing phantom
                            // balances to persist after ISSUE undo during reorgs.
                            undo.from_address = issuer;
                            
                            auto undo_key = UndoKey(height, txhash, tx_op_index++);
                            batch.Write(undo_key, undo);
                            LogDebug(BCLog::TOKEN, "Created undo: %s\n", FormatUndoRecord(undo, height));
                            
                            block_token_txs.push_back(txhash);
                            ops_count++;
                            issue_count++;
                            issue_count_this_tx++;
                        }
                    }
                    break;
                }
                
                case src20::TokenAction::TRANSFER: {
                    const auto* transfer = op.GetTransfer();
                    if (transfer && transfer->IsValid()) {
                        auto recipient = src20::GetTransferRecipient(*tx);
                        if (recipient) {
                            // Use sender derived from blockundo
                            CScript sender = tx_sender;
                            
                            // AUDIT FIX [C-01/C-02]: Read balances from overlay to get
                            // correct values reflecting prior ops in this block.
                            uint64_t prev_from_balance = GetBalanceWithOverlay(*this, overlay, sender, transfer->token_id);
                            uint64_t prev_to_balance = GetBalanceWithOverlay(*this, overlay, *recipient, transfer->token_id);
                            
                            auto token_info = GetTokenInfoWithOverlay(*this, overlay, transfer->token_id);
                            uint64_t prev_holder_count = token_info ? token_info->holder_count : 0;
                            
                            const uint256 txhash = tx->GetHash().ToUint256();
                            if (TransferTokens(transfer->token_id, sender, *recipient,
                                             transfer->amount, txhash, height, block_time, &batch, tx_op_index, &overlay)) {
                                // Create undo record for transfer
                                TokenUndoRecord undo;
                                undo.op_type = TokenOpType::TRANSFER;
                                undo.txid = txhash;
                                undo.token_id = transfer->token_id;
                                undo.from_address = sender;
                                undo.to_address = *recipient;
                                undo.amount = transfer->amount;
                                undo.prev_from_balance = prev_from_balance;
                                undo.prev_to_balance = prev_to_balance;
                                undo.prev_holder_count = prev_holder_count;
                                
                                auto undo_key = UndoKey(height, txhash, tx_op_index++);
                                batch.Write(undo_key, undo);
                                LogDebug(BCLog::TOKEN, "Created undo: %s\n", FormatUndoRecord(undo, height));
                                
                                block_token_txs.push_back(txhash);
                                ops_count++;
                                txfr_count++;
                            }
                        }
                    }
                    break;
                }
                
                case src20::TokenAction::BURN: {
                    const auto* burn = op.GetBurn();
                    if (burn && burn->IsValid()) {
                        // Use sender derived from blockundo as the burner
                        CScript burner = tx_sender;
                        
                        // AUDIT FIX [C-01/C-02]: Read state from overlay to get
                        // correct values reflecting prior ops in this block.
                        auto token_info = GetTokenInfoWithOverlay(*this, overlay, burn->token_id);
                        uint64_t prev_circulating = token_info ? token_info->circulating_supply : 0;
                        // AUDIT FIX [v4-ISSUE-001]: Capture prev_holder_count BEFORE BurnTokens()
                        // so DisconnectBlock can restore it.  Previously omitted, causing reorg
                        // to reset holder_count to 0 (the TokenUndoRecord default).
                        uint64_t prev_holder_count = token_info ? token_info->holder_count : 0;
                        uint64_t prev_balance = GetBalanceWithOverlay(*this, overlay, burner, burn->token_id);
                        
                        const uint256 txhash = tx->GetHash().ToUint256();
                        if (BurnTokens(burn->token_id, burner, burn->amount,
                                      txhash, height, block_time, &batch, tx_op_index, &overlay)) {
                            // Create undo record for burn
                            TokenUndoRecord undo;
                            undo.op_type = TokenOpType::BURN;
                            undo.txid = txhash;
                            undo.token_id = burn->token_id;
                            undo.from_address = burner;
                            undo.amount = burn->amount;
                            undo.prev_from_balance = prev_balance;
                            undo.prev_circulating_supply = prev_circulating;
                            undo.prev_holder_count = prev_holder_count;
                            
                            auto undo_key = UndoKey(height, txhash, tx_op_index++);
                            batch.Write(undo_key, undo);
                            LogDebug(BCLog::TOKEN, "Created undo: %s\n", FormatUndoRecord(undo, height));
                            
                            block_token_txs.push_back(txhash);
                            ops_count++;
                            txfr_count++;
                        }
                    }
                    break;
                }
                
                default:
                    break;
            }
        }
    }
    
    // Record block's token transactions for reorg handling
    // AUDIT FIX [R12-04]: Deduplicate txids before writing. Multi-op transactions
    // push their txid once per operation, wasting DB space. DisconnectBlock
    // already deduplicates on read, but deduplicating at write time is cleaner.
    if (!block_token_txs.empty()) {
        std::vector<uint256> deduped_txs;
        std::set<uint256> seen_txs;
        for (const auto& txid : block_token_txs) {
            if (seen_txs.insert(txid).second) {
                deduped_txs.push_back(txid);
            }
        }
        auto key = BlockTokensKey(height);
        batch.Write(key, deduped_txs);
    }
    
    // AUDIT FIX [H-03]: ALL token ops + undo records committed in single atomic WriteBatch.
    // Previously, RegisterToken/TransferTokens/BurnTokens each committed separately,
    // then undo records were committed in another batch — partial state on crash.
    //
    // SECURITY FIX [C-02b]: Include best-block pointer in the same atomic batch.
    // Previously SetBestBlock was a separate DB write after WriteBatch. A crash
    // between the two would leave token state updated but best-block stale,
    // causing double-processing of the same block on restart (token inflation).
    batch.Write(BestBlockKey(), block.GetHash());
    
    try {
        m_db->WriteBatch(batch);
    } catch (const dbwrapper_error& e) {
        LogPrintf("CRITICAL: Failed to write token operations for block %d: %s\n", height, e.what());
        throw;  // Re-throw to signal block processing failure
    }
    
    // FIX [R5-03]: Update atomic counters AFTER WriteBatch succeeds.
    // Sub-functions (RegisterToken, TransferTokens, BurnTokens) skip counter
    // updates when external_batch is provided, deferring to this post-commit point.
    // This prevents counter drift if WriteBatch throws.
    // AUDIT FIX [R13-03]: Lock m_count_cs to serialize with lazy GetTokenCount/
    // GetTransferCount initialization, preventing permanent undercount when a
    // concurrent scan and this increment race on the initialized flag.
    {
        LOCK(m_count_cs);
        if (m_token_count_initialized.load(std::memory_order_acquire) && issue_count > 0) {
            m_token_count_cache.fetch_add(issue_count);
        }
        if (m_transfer_count_initialized.load(std::memory_order_acquire) && txfr_count > 0) {
            m_transfer_count_cache.fetch_add(txfr_count);
        }
    }

    // AUDIT FIX [M-01/M-02]: Now that WriteBatch succeeded, flush overlay
    // token_infos to the in-memory LRU cache. Doing this AFTER commit ensures
    // the cache never contains data that isn't on disk.
    for (const auto& [tid, tinfo] : overlay.token_infos) {
        UpdateCache(tinfo);
    }
    
    if (ops_count > 0) {
        LogDebug(BCLog::TOKEN, "Processed %d token operations in block %d\n", ops_count, height);
    }
    
    return ops_count;
}

// AUDIT NOTE [I-04]: This is the legacy ProcessBlock overload without CBlockUndo.
// It cannot accurately determine sender addresses for transfers/burns.
// Prefer using ProcessBlock(block, height, blockundo) when blockundo is available.
// This overload exists for backwards compatibility with older code paths.
int TokenDB::ProcessBlock(const CBlock& block, int height, const CCoinsViewCache* view)
{
    if (!m_db) return 0;

    // CRITICAL FIX [R5-01]: Reprocessing guard (same as CBlockUndo overload).
    // See detailed comment in the CBlockUndo version above.
    {
        uint256 best_block = GetBestBlock();
        if (best_block == block.GetHash()) {
            LogPrintf("Token ProcessBlock(view): block %d already processed (BestBlock matches %s), skipping reprocess\n",
                      height, best_block.ToString().substr(0, 16));
            return 0;
        }
    }

    int ops_count = 0;
    // FIX [R5-03]: Track per-type counts for post-commit counter updates.
    int issue_count = 0;
    int txfr_count = 0;
    int64_t block_time = block.GetBlockTime();
    std::vector<uint256> block_token_txs;
    CDBBatch batch(*m_db);

    // AUDIT FIX [C-01/C-02]: In-memory balance overlay prevents stale reads.
    // Same fix as the main ProcessBlock(CBlockUndo) overload — without this,
    // two transfers from the same address in one block each read the original
    // balance from DB, enabling double-spend (token inflation).
    BalanceOverlay overlay;

    // AUDIT FIX [M-04]: Enforce MAX_TOKENS_PER_BLOCK at the processing level.
    static constexpr size_t MAX_OPS = src20::MAX_TOKENS_PER_BLOCK;
    
    unsigned int tx_index = 0;
    for (const auto& tx : block.vtx) {
        if (static_cast<size_t>(ops_count) >= MAX_OPS) break;
        auto ops = src20::ParseTransactionSRC20(*tx);

        // DEFENSE-IN-DEPTH [R7-D3]: Skip coinbase and guard against index-0,
        // mirroring the CBlockUndo overload's [R4-02] fix.  Without this, a
        // malformed block with a non-coinbase at index 0 would proceed to
        // derive a sender from the UTXO view (possibly valid) and process
        // token ops from a "transaction" that shouldn't contain them.
        if (tx->IsCoinBase() || tx_index == 0) {
            if (!ops.empty()) {
                LogDebug(BCLog::TOKEN, "Skipping token ops in coinbase/index-0 tx (view overload)\n");
            }
            tx_index++;
            continue;
        }
        tx_index++;

        // Derive sender from first input's spent UTXO
        CScript tx_sender;
        if (view && !tx->vin.empty() && !tx->IsCoinBase()) {
            const COutPoint& prevout = tx->vin[0].prevout;
            const Coin& coin = view->AccessCoin(prevout);
            if (!coin.IsSpent()) {
                tx_sender = coin.out.scriptPubKey;
            }
        }
        
        // SECURITY FIX [C-02]: Track per-tx operation index (same as blockundo version)
        // AUDIT FIX [L-04]: Widened from uint8_t to uint16_t to prevent wrap at 256 ops per tx.
        uint16_t tx_op_index = 0;
        // AUDIT FIX [R14-F01]: Track per-TX issuance count for scaled fee check.
        int issue_count_this_tx = 0;

        for (const auto& op : ops) {
            // AUDIT FIX [ISSUE-012]: Check MAX_OPS inside the inner ops loop too.
            // The outer per-tx check only fires between transactions; without
            // this, a transaction with multiple ops could push ops_count past
            // MAX_OPS within a single tx iteration.
            if (static_cast<size_t>(ops_count) >= MAX_OPS) break;
            switch (op.action) {
                case src20::TokenAction::ISSUE: {
                    const auto* issuance = op.GetIssuance();
                    if (issuance && issuance->IsValid()) {
                        // AUDIT FIX [H-04]: Enforce minimum issuance fee to prevent token spam
                        // AUDIT FIX [M-03]: Coinbase transactions cannot issue tokens.
                        if (tx->IsCoinBase()) {
                            LogDebug(BCLog::TOKEN, "Token issuance rejected: coinbase transactions cannot issue tokens\n");
                            break;
                        }
                        if (view && !tx->vin.empty()) {
                            CAmount total_in = 0;
                            for (const auto& vin : tx->vin) {
                                const Coin& coin = view->AccessCoin(vin.prevout);
                                if (!coin.IsSpent()) {
                                    total_in += coin.out.nValue;
                                }
                            }
                            CAmount total_out = 0;
                            for (const auto& out : tx->vout) {
                                total_out += out.nValue;
                            }
                            CAmount tx_fee = total_in - total_out;
                            // AUDIT FIX [R14-F01]: Require fee proportional to issuance count.
                            CAmount required_fee = src20::MIN_TOKEN_ISSUANCE_FEE * (issue_count_this_tx + 1);
                            if (tx_fee < required_fee) {
                                LogDebug(BCLog::TOKEN, "Token issuance rejected: fee %lld < minimum %lld (issuance #%d in tx)\n",
                                         tx_fee, required_fee, issue_count_this_tx + 1);
                                break;
                            }
                        } else if (!view || tx->vin.empty()) {
                            // AUDIT FIX [M-04]: Without a UTXO view we cannot verify the
                            // issuance fee. Reject the issuance rather than silently skipping
                            // the fee check, which would let miners issue tokens for free via
                            // the legacy code path.
                            // AUDIT FIX [R19-03]: Also reject when vin is empty — previously
                            // the condition `view && !tx->vin.empty()` was false AND
                            // `!view` was false, letting empty-vin non-coinbase txs bypass
                            // the fee check entirely.
                            LogDebug(BCLog::TOKEN, "Token issuance rejected: no UTXO view or empty vin to verify fee\n");
                            break;
                        }
                        
                        // Get issuer address from first non-OP_RETURN output
                        CScript issuer;
                        for (const auto& vout : tx->vout) {
                            if (!vout.scriptPubKey.IsUnspendable()) {
                                issuer = vout.scriptPubKey;
                                break;
                            }
                        }

                        // DEFENSE-IN-DEPTH [R7-D1]: Reject issuance with no
                        // spendable outputs. All outputs being OP_RETURN means
                        // the entire supply is credited to an empty CScript,
                        // making the tokens permanently unreachable while
                        // showing a misleading holder_count of 1.
                        if (issuer.empty()) {
                            LogDebug(BCLog::TOKEN, "Token issuance rejected: no spendable output for issuer address\n");
                            break;
                        }

                        // AUDIT FIX [ISSUE-001]: Reject duplicate tickers within the same block.
                        if (overlay.pending_tickers.count(issuance->ticker)) {
                            LogPrintf("Token issuance rejected: duplicate ticker '%s' within block at height %d\n",
                                      issuance->ticker, height);
                            break;
                        }
                        
                        const uint256 txhash = tx->GetHash().ToUint256();
                        // AUDIT FIX [v4-ISSUE-002]: Pass tx_op_index for correct TransferKey.
                        auto token_id_opt = RegisterToken(*issuance, txhash, issuer, height, block_time, &batch, &overlay, tx_op_index);
                        if (token_id_opt) {
                            overlay.pending_tickers.insert(issuance->ticker);
                            // Create undo record for token issuance
                            TokenUndoRecord undo;
                            undo.op_type = TokenOpType::ISSUE;
                            undo.txid = txhash;
                            undo.token_id = *token_id_opt;
                            undo.ticker = issuance->ticker;
                            // AUDIT FIX [H-01]: Store issuer address for reorg cleanup.
                            undo.from_address = issuer;
                            
                            auto undo_key = UndoKey(height, txhash, tx_op_index++);
                            batch.Write(undo_key, undo);
                            LogDebug(BCLog::TOKEN, "Created undo: %s\n", FormatUndoRecord(undo, height));
                            
                            block_token_txs.push_back(txhash);
                            ops_count++;
                            issue_count++;
                            issue_count_this_tx++;
                        }
                    }
                    break;
                }
                
                case src20::TokenAction::TRANSFER: {
                    const auto* transfer = op.GetTransfer();
                    if (transfer && transfer->IsValid()) {
                        auto recipient = src20::GetTransferRecipient(*tx);
                        if (recipient) {
                            // Use sender derived from UTXO
                            CScript sender = tx_sender;
                            
                            // AUDIT FIX [C-01/C-02]: Read balances from overlay to get
                            // correct values reflecting prior ops in this block.
                            uint64_t prev_from_balance = GetBalanceWithOverlay(*this, overlay, sender, transfer->token_id);
                            uint64_t prev_to_balance = GetBalanceWithOverlay(*this, overlay, *recipient, transfer->token_id);
                            
                            auto token_info = GetTokenInfoWithOverlay(*this, overlay, transfer->token_id);
                            uint64_t prev_holder_count = token_info ? token_info->holder_count : 0;
                            
                            const uint256 txhash = tx->GetHash().ToUint256();
                            if (TransferTokens(transfer->token_id, sender, *recipient,
                                             transfer->amount, txhash, height, block_time, &batch, tx_op_index, &overlay)) {
                                // Create undo record for transfer
                                TokenUndoRecord undo;
                                undo.op_type = TokenOpType::TRANSFER;
                                undo.txid = txhash;
                                undo.token_id = transfer->token_id;
                                undo.from_address = sender;
                                undo.to_address = *recipient;
                                undo.amount = transfer->amount;
                                undo.prev_from_balance = prev_from_balance;
                                undo.prev_to_balance = prev_to_balance;
                                undo.prev_holder_count = prev_holder_count;
                                
                                auto undo_key = UndoKey(height, txhash, tx_op_index++);
                                batch.Write(undo_key, undo);
                                LogDebug(BCLog::TOKEN, "Created undo: %s\n", FormatUndoRecord(undo, height));
                                
                                block_token_txs.push_back(txhash);
                                ops_count++;
                                txfr_count++;
                            }
                        }
                    }
                    break;
                }
                
                case src20::TokenAction::BURN: {
                    const auto* burn = op.GetBurn();
                    if (burn && burn->IsValid()) {
                        // Use sender derived from UTXO as the burner
                        CScript burner = tx_sender;
                        
                        // AUDIT FIX [C-01/C-02]: Read state from overlay to get
                        // correct values reflecting prior ops in this block.
                        auto token_info = GetTokenInfoWithOverlay(*this, overlay, burn->token_id);
                        uint64_t prev_circulating = token_info ? token_info->circulating_supply : 0;
                        // AUDIT FIX [v4-ISSUE-001]: Capture prev_holder_count BEFORE BurnTokens()
                        uint64_t prev_holder_count = token_info ? token_info->holder_count : 0;
                        uint64_t prev_balance = GetBalanceWithOverlay(*this, overlay, burner, burn->token_id);
                        
                        const uint256 txhash = tx->GetHash().ToUint256();
                        if (BurnTokens(burn->token_id, burner, burn->amount,
                                      txhash, height, block_time, &batch, tx_op_index, &overlay)) {
                            // Create undo record for burn
                            TokenUndoRecord undo;
                            undo.op_type = TokenOpType::BURN;
                            undo.txid = txhash;
                            undo.token_id = burn->token_id;
                            undo.from_address = burner;
                            undo.amount = burn->amount;
                            undo.prev_from_balance = prev_balance;
                            undo.prev_circulating_supply = prev_circulating;
                            undo.prev_holder_count = prev_holder_count;
                            
                            auto undo_key = UndoKey(height, txhash, tx_op_index++);
                            batch.Write(undo_key, undo);
                            LogDebug(BCLog::TOKEN, "Created undo: %s\n", FormatUndoRecord(undo, height));
                            
                            block_token_txs.push_back(txhash);
                            ops_count++;
                            txfr_count++;
                        }
                    }
                    break;
                }
                
                default:
                    break;
            }
        }
    }
    
    // Record block's token transactions for reorg handling
    // AUDIT FIX [R12-04]: Deduplicate txids before writing (matches primary overload).
    if (!block_token_txs.empty()) {
        std::vector<uint256> deduped_txs;
        std::set<uint256> seen_txs;
        for (const auto& txid : block_token_txs) {
            if (seen_txs.insert(txid).second) {
                deduped_txs.push_back(txid);
            }
        }
        auto key = BlockTokensKey(height);
        batch.Write(key, deduped_txs);
    }
    
    // AUDIT FIX [H-03]: ALL token ops + undo records committed in single atomic WriteBatch.
    // AUDIT FIX [H-02]: Include best-block in the same atomic WriteBatch.
    // Previously SetBestBlock was a separate DB write. A crash between
    // WriteBatch and SetBestBlock would leave token state updated but
    // best-block stale, causing double-processing on restart (token inflation).
    batch.Write(BestBlockKey(), block.GetHash());
    try {
        m_db->WriteBatch(batch);
    } catch (const dbwrapper_error& e) {
        LogPrintf("CRITICAL: Failed to write token operations for block %d: %s\n", height, e.what());
        throw;  // Re-throw to signal block processing failure
    }

    // FIX [R5-03]: Update atomic counters AFTER WriteBatch succeeds (same as primary overload).
    // AUDIT FIX [R13-03]: Lock m_count_cs to serialize with lazy count initialization.
    {
        LOCK(m_count_cs);
        if (m_token_count_initialized.load(std::memory_order_acquire) && issue_count > 0) {
            m_token_count_cache.fetch_add(issue_count);
        }
        if (m_transfer_count_initialized.load(std::memory_order_acquire) && txfr_count > 0) {
            m_transfer_count_cache.fetch_add(txfr_count);
        }
    }

    // AUDIT FIX [M-01/M-02]: Flush overlay token_infos to LRU cache after commit.
    for (const auto& [tid, tinfo] : overlay.token_infos) {
        UpdateCache(tinfo);
    }
    
    if (ops_count > 0) {
        LogDebug(BCLog::TOKEN, "Processed %d token operations in block %d\n", ops_count, height);
    }
    
    return ops_count;
}

bool TokenDB::DisconnectBlock(const CBlock& block, int height)
{
    if (!m_db) return false;

    // CRITICAL FIX [R5-01]: Guard against re-disconnect after crash.
    // If the node crashes after token DisconnectBlock commits but before the
    // UTXO view is flushed, on restart the UTXO tip is still at block N while
    // token DB has already rolled back to N-1. The chain state code would
    // attempt DisconnectTip again, re-running DisconnectBlock. Without this
    // guard, the undo records (already deleted in the first run) would not be
    // found, and the best-block would be mis-set to the grandparent.
    {
        uint256 best_block = GetBestBlock();
        if (best_block == block.hashPrevBlock) {
            LogPrintf("Token DisconnectBlock: block %d already disconnected (BestBlock matches prev %s), skipping\n",
                      height, best_block.ToString().substr(0, 16));
            return true;
        }
    }

    // Read the token transactions from this block
    auto key = BlockTokensKey(height);
    std::vector<uint256> block_token_txs;
    if (!m_db->Read(key, block_token_txs)) {
        // AUDIT FIX [R25-03]: Distinguish "key not found" from "corrupt record".
        // Previously, both cases returned true (treat as "no token ops"), but a
        // corrupt BlockTokens record means undo records DO exist and would be
        // silently skipped — causing token state to diverge after a reorg.
        if (m_db->Exists(key)) {
            LogPrintf("ERROR: BlockTokens record at height %d exists but failed to deserialize. "
                      "Undo records will NOT be applied — token state may have DIVERGED. "
                      "Consider running with -reindex.\n", height);
            return false;
        }
        // Key genuinely does not exist — no token transactions in this block
        return true;
    }
    
    CDBBatch batch(*m_db);
    int undo_count = 0;
    // FIX [R5-03b]: Track per-type undo counts for post-commit counter updates,
    // mirroring the R5-03 pattern in ProcessBlock.  Previously, counters were
    // decremented inside the switch cases BEFORE WriteBatch, causing drift
    // if WriteBatch failed.
    int issue_undo_count = 0;
    int txfr_undo_count = 0;
    
    // AUDIT FIX [ISSUE-003]: Deduplicate block_token_txs.
    // ProcessBlock pushes the txid once per *operation*, so a tx with 3 ops
    // appears 3 times.  The inner op_idx loop already handles multi-op txs,
    // so duplicates only cause redundant passes and spurious warnings.
    {
        std::vector<uint256> deduped;
        deduped.reserve(block_token_txs.size());
        std::set<uint256> seen;
        for (const auto& txid : block_token_txs) {
            if (seen.insert(txid).second) {
                deduped.push_back(txid);
            }
        }
        block_token_txs = std::move(deduped);
    }
    
    // AUDIT FIX [ISSUE-004]: In-memory overlay for TokenInfo during disconnect.
    // Without this, multiple undo operations for the same token in one
    // DisconnectBlock each call GetTokenInfo() from DB and overwrite each
    // other's changes (e.g. transfer_count gets decremented only once instead
    // of N times).
    std::map<src20::TokenId, TokenInfo> disconnect_info_overlay;
    
    // AUDIT FIX [v4-ISSUE-003]: In-memory overlay for addr_tokens during disconnect.
    // Without this, multiple undo operations for the same address read stale
    // addr_tokens from the DB (CDBBatch writes are not visible until WriteBatch),
    // causing later undos to overwrite earlier ones and leaving phantom entries.
    std::map<CScript, std::set<src20::TokenId>> disconnect_addr_overlay;
    
    // Helper: get token info from overlay first, then DB
    auto GetInfoForDisconnect = [&](const src20::TokenId& tid) -> std::optional<TokenInfo> {
        auto it = disconnect_info_overlay.find(tid);
        if (it != disconnect_info_overlay.end()) return it->second;
        return GetTokenInfo(tid);
    };
    
    // Helper: get addr_tokens from overlay first, then DB
    auto GetAddrTokensForDisconnect = [&](const CScript& addr) -> std::set<src20::TokenId> {
        auto it = disconnect_addr_overlay.find(addr);
        if (it != disconnect_addr_overlay.end()) return it->second;
        std::set<src20::TokenId> tokens;
        m_db->Read(AddrTokensKey(addr), tokens);
        return tokens;
    };
    
    // AUDIT FIX [R9-01]: Collect token IDs to invalidate from cache AFTER
    // WriteBatch succeeds.  Previously InvalidateCache() was called inside the
    // switch cases *before* WriteBatch, so a concurrent reader could
    // repopulate the LRU cache with stale pre-disconnect DB data.
    std::set<src20::TokenId> tokens_to_invalidate;

    // Process transactions in reverse order to properly undo
    for (auto it = block_token_txs.rbegin(); it != block_token_txs.rend(); ++it) {
        const uint256& txid = *it;
        
        // SECURITY FIX [C-02]: Iterate all op_indices for this txid.
        // A single transaction may have multiple token operations, each with
        // a unique undo record keyed by (height, txid, op_index).
        // AUDIT FIX [L-04]: Widened to uint16_t to match ProcessBlock key width.
        // AUDIT FIX [ISSUE-009]: Tightened from 65535.  ParseTransactionSRC20()
        // caps each tx to MAX_OPS_PER_TX ops.
        // AUDIT FIX [R19-04]: Derive from the shared header constant instead of
        // an independent magic number.  If MAX_OPS_PER_TX is ever raised but
        // this constant is not updated, undo records would be silently dropped
        // during reorgs, causing irreversible balance corruption.
        static constexpr uint16_t MAX_UNDO_OPS_PER_TX = src20::MAX_OPS_PER_TX * 2;
        
        // AUDIT FIX [ISSUE-006]: Collect undo records first, then process in
        // reverse op_idx order.  Operations were applied in ascending op_idx
        // order during ProcessBlock, so undoing them requires descending order
        // to avoid intermediate state corruption (e.g. restoring a balance
        // before restoring the supply that depends on it).
        std::vector<std::pair<uint16_t, TokenUndoRecord>> tx_undos;
        for (uint16_t op_idx = 0; op_idx < MAX_UNDO_OPS_PER_TX; op_idx++) {
            auto undo_key = UndoKey(height, txid, op_idx);
            TokenUndoRecord undo;
            if (!m_db->Read(undo_key, undo)) {
                break;
            }
            tx_undos.emplace_back(op_idx, std::move(undo));
        }
        
        if (tx_undos.empty()) {
            LogPrintf("Warning: No undo record for token tx %s at height %d\n",
                     txid.ToString().substr(0, 16), height);
            continue;  // next txid
        }
        
        // Process in reverse order
        for (auto rit = tx_undos.rbegin(); rit != tx_undos.rend(); ++rit) {
            uint16_t op_idx = rit->first;
            const TokenUndoRecord& undo = rit->second;
        
            // SECURITY FIX [L-05]: Fixed misleading indentation. The LogDebug, switch
            // statement, and undo record deletion below are all inside the inner
            // for(op_idx) loop, but were previously indented at the outer for-loop
            // level, making the code appear as if they executed outside the loop.

            // Log comprehensive undo information for debugging reorgs
            LogDebug(BCLog::TOKEN, "%s\n", FormatUndoRecord(undo, height));
        
            switch (undo.op_type) {
                case TokenOpType::ISSUE: {
                    // Remove the token entirely
                    LogDebug(BCLog::TOKEN, "Undoing token issuance: %s (ticker: %s)\n",
                            undo.token_id.ToString().substr(0, 16), undo.ticker);
                
                    // Delete token info
                    auto token_key = TokenInfoKey(undo.token_id);
                    batch.Erase(token_key);
                
                    // Delete ticker index
                    auto ticker_key = TickerKey(undo.ticker);
                    batch.Erase(ticker_key);
                
                    // Defer cache invalidation until after WriteBatch (R9-01)
                    tokens_to_invalidate.insert(undo.token_id);
                
                    // FIX [R5-03b]: Count ISSUE undos; actual decrement deferred
                    // to after WriteBatch succeeds (matching ProcessBlock pattern).
                    issue_undo_count++;
                
                    // SECURITY FIX [M-08]: Clean up issuer's balance and address token list
                    // Previously only token info and ticker index were deleted, leaving
                    // phantom balances and stale address-token mappings after reorg.
                    if (!undo.from_address.empty()) {
                        // Delete issuer's balance entry for this token
                        batch.Erase(BalanceKey(undo.from_address, undo.token_id));
                        // Remove holder index entry
                        batch.Erase(TokenHoldersKey(undo.token_id, undo.from_address));
                        // Remove token from issuer's address token list
                        // AUDIT FIX [v4-ISSUE-003]: Use disconnect addr overlay
                        auto addr_key = AddrTokensKey(undo.from_address);
                        std::set<src20::TokenId> addr_tokens = GetAddrTokensForDisconnect(undo.from_address);
                        addr_tokens.erase(undo.token_id);
                        if (addr_tokens.empty()) {
                            batch.Erase(addr_key);
                        } else {
                            batch.Write(addr_key, addr_tokens);
                        }
                        disconnect_addr_overlay[undo.from_address] = addr_tokens;
                    }
                
                    // AUDIT FIX [ISSUE-011]: Delete the issuance's TransferRecord.
                    // RegisterToken() writes a TransferRecord to record the issuance
                    // in history.  Without this erase, stale issuance records remain
                    // visible in token history after a reorg undoes the issuance.
                    {
                        auto issue_transfer_key = TransferKey(undo.token_id, height, txid, op_idx);
                        batch.Erase(issue_transfer_key);
                    }
                
                    undo_count++;
                    break;
                }
            
                case TokenOpType::TRANSFER: {
                    // Restore previous balances
                    LogDebug(BCLog::TOKEN, "Undoing token transfer: %s amount=%lu\n",
                            undo.token_id.ToString().substr(0, 16), undo.amount);
                
                    // AUDIT FIX [L-04]: Sanity check for balance restoration
                    // Verify prev_from_balance is logically valid (should be >= amount transferred)
                    if (undo.prev_from_balance < undo.amount) {
                        LogPrintf("WARNING: Token %s disconnect: prev_from_balance (%lu) < transfer amount (%lu)\n",
                                  undo.token_id.ToString().substr(0, 16), undo.prev_from_balance, undo.amount);
                    }
                
                    // Restore sender's balance (to batch for atomicity)
                    WriteBalanceToBatch(batch, undo.from_address, undo.token_id, undo.prev_from_balance);
                
                    // SPOT-CHECK FIX: If the forward transfer completely drained the
                    // sender (prev_from_balance == amount → post-transfer balance was 0),
                    // TransferTokens() erased the sender's holder index key AND removed
                    // the token from their addr_tokens.  Undo must restore addr_tokens.
                    // NOTE: WriteBalanceToBatch above already re-wrote the holder key
                    // (prev_from_balance > 0 → holder key written), so we only need
                    // to restore addr_tokens here.
                    if (undo.prev_from_balance == undo.amount && !undo.from_address.empty()) {
                        // Re-add token to sender's addr_tokens via overlay
                        auto from_tokens = GetAddrTokensForDisconnect(undo.from_address);
                        from_tokens.insert(undo.token_id);
                        batch.Write(AddrTokensKey(undo.from_address), from_tokens);
                        disconnect_addr_overlay[undo.from_address] = from_tokens;
                    }
                
                    // Restore recipient's balance (to batch for atomicity)
                    WriteBalanceToBatch(batch, undo.to_address, undo.token_id, undo.prev_to_balance);
                
                    // AUDIT FIX [ISSUE-008]: If the recipient had zero balance before the
                    // transfer (i.e. was a new holder), remove the token from their addr_tokens
                    // set to mirror the forward-path logic in TransferTokens().
                    if (undo.prev_to_balance == 0 && !undo.to_address.empty()) {
                        // AUDIT FIX [v4 ISSUE-003]: Use disconnect overlay for addr_tokens
                        // to prevent stale reads when multiple undos affect the same address.
                        auto to_tokens = GetAddrTokensForDisconnect(undo.to_address);
                        to_tokens.erase(undo.token_id);
                        auto to_addr_key = AddrTokensKey(undo.to_address);
                        if (to_tokens.empty()) {
                            batch.Erase(to_addr_key);
                        } else {
                            batch.Write(to_addr_key, to_tokens);
                        }
                        disconnect_addr_overlay[undo.to_address] = to_tokens;
                    }
                
                    // Restore holder count and transfer count (to batch)
                    // AUDIT FIX [ISSUE-004]: Use disconnect overlay instead of GetTokenInfo()
                    // to prevent stale reads when multiple ops affect the same token.
                    auto token_info = GetInfoForDisconnect(undo.token_id);
                    if (token_info) {
                        token_info->holder_count = undo.prev_holder_count;
                        // SECURITY: Safe underflow handling for transfer count
                        // transfer_count could be 0 if database was corrupted
                        if (token_info->transfer_count > 0) {
                            token_info->transfer_count--;
                        } else {
                            LogPrintf("WARNING: Token %s transfer_count already 0 during disconnect\n",
                                      undo.token_id.ToString().substr(0, 16));
                        }
                        // AUDIT FIX [R9-01b]: Pass update_cache=false to avoid
                        // briefly inserting uncommitted data into the LRU cache.
                        WriteTokenInfoToBatch(batch, *token_info, /*update_cache=*/false);
                        disconnect_info_overlay[undo.token_id] = *token_info;
                    }
                
                    // AUDIT FIX [M-01]: Include op_index in TransferKey for correct deletion.
                    // Previously op_index defaulted to 0, leaving orphaned transfer records
                    // for multi-op transactions after reorgs.
                    auto transfer_key = TransferKey(undo.token_id, height, txid, op_idx);
                    batch.Erase(transfer_key);
                
                    // FIX [R5-03b]: Count TRANSFER undos; actual decrement deferred
                    // to after WriteBatch succeeds (matching ProcessBlock pattern).
                    txfr_undo_count++;
                
                    // Defer cache invalidation until after WriteBatch (R9-01)
                    tokens_to_invalidate.insert(undo.token_id);
                    undo_count++;
                    break;
                }
            
                case TokenOpType::BURN: {
                    // Restore burned tokens
                    LogDebug(BCLog::TOKEN, "Undoing token burn: %s amount=%lu\n",
                            undo.token_id.ToString().substr(0, 16), undo.amount);
                
                    // Restore burner's balance (to batch for atomicity)
                    WriteBalanceToBatch(batch, undo.from_address, undo.token_id, undo.prev_from_balance);
                
                    // AUDIT FIX [ISSUE-005]: Restore holder_count from undo record.
                    // If the burn set the burner's balance to zero, BurnTokens()
                    // decremented holder_count and erased the holder key.  Reversing
                    // the burn must restore holder_count; the holder key is already
                    // re-written by WriteBalanceToBatch above (prev_from_balance > 0).
                
                    // SPOT-CHECK FIX: If the burn completely drained the burner
                    // (prev_from_balance == amount → post-burn balance was 0),
                    // BurnTokens() removed the token from their addr_tokens.
                    // Undo must re-add it.
                    if (undo.prev_from_balance == undo.amount && !undo.from_address.empty()) {
                        auto from_tokens = GetAddrTokensForDisconnect(undo.from_address);
                        from_tokens.insert(undo.token_id);
                        batch.Write(AddrTokensKey(undo.from_address), from_tokens);
                        disconnect_addr_overlay[undo.from_address] = from_tokens;
                    }
                
                    // AUDIT FIX [ISSUE-004]: Use disconnect overlay instead of GetTokenInfo()
                    auto token_info = GetInfoForDisconnect(undo.token_id);
                    if (token_info) {
                        token_info->circulating_supply = undo.prev_circulating_supply;
                        // AUDIT FIX [ISSUE-005]: Restore holder_count from undo record
                        token_info->holder_count = undo.prev_holder_count;
                        // AUDIT FIX [R9-01b]: Pass update_cache=false to avoid
                        // briefly inserting uncommitted data into the LRU cache.
                        WriteTokenInfoToBatch(batch, *token_info, /*update_cache=*/false);
                        disconnect_info_overlay[undo.token_id] = *token_info;
                    }
                
                    // AUDIT FIX [M-02]: Delete the burn's transfer record.
                    // BurnTokens() writes a TokenTransferRecord to record the burn in history,
                    // but the BURN undo path previously didn't erase it, leaving stale burn
                    // records visible in history after reorg.
                    {
                        auto burn_transfer_key = TransferKey(undo.token_id, height, txid, op_idx);
                        batch.Erase(burn_transfer_key);
                    }

                    // FIX [R5-03b]: Count BURN undos (burns count as transfers);
                    // actual decrement deferred to after WriteBatch succeeds.
                    txfr_undo_count++;
                
                    // Defer cache invalidation until after WriteBatch (R9-01)
                    tokens_to_invalidate.insert(undo.token_id);
                    undo_count++;
                    break;
                }
            
                default:
                    LogPrintf("Warning: Unknown undo operation type in tx %s\n",
                             txid.ToString().substr(0, 16));
                    break;
            }
        
            // Delete the undo record
            auto undo_key = UndoKey(height, txid, op_idx);
            batch.Erase(undo_key);
        } // end reverse op_index loop [ISSUE-006]
    }
    
    // Remove block record
    batch.Erase(key);
    
    // SECURITY FIX [H-10]: Update best-block pointer to the previous block in the
    // same atomic batch. Previously DisconnectBlock never updated the best-block
    // marker, so a crash during reorg left the token DB pointing to an orphaned
    // block with partially disconnected state — unrecoverable without full reindex.
    batch.Write(BestBlockKey(), block.hashPrevBlock);
    
    // FIX [R5-04]: Wrap WriteBatch in try-catch. Without this, a LevelDB
    // write failure would propagate as an unhandled exception, potentially
    // crashing the node. Token disconnect is non-consensus, so we log and
    // return false to let the caller handle it gracefully.
    try {
        m_db->WriteBatch(batch);
    } catch (const dbwrapper_error& e) {
        LogPrintf("CRITICAL: Failed to write token disconnect for block %d: %s. "
                  "Token state may have DIVERGED. Consider -reindex.\n", height, e.what());
        return false;
    }
    
    // AUDIT FIX [R9-01]: Invalidate LRU cache entries AFTER WriteBatch succeeds.
    // This matches ProcessBlock's "cache after commit" pattern and closes the
    // race where a concurrent reader could repopulate the cache with stale
    // pre-disconnect data between InvalidateCache() and WriteBatch().
    for (const auto& token_id : tokens_to_invalidate) {
        InvalidateCache(token_id);
    }

    // FIX [R5-03b]: Update atomic counters AFTER WriteBatch succeeds.
    // Mirrors the R5-03 pattern in ProcessBlock.  Previously, counters were
    // decremented inside the switch cases before WriteBatch, so a failed
    // WriteBatch would leave counters decremented with no matching DB change.
    // AUDIT FIX [R13-03]: Lock m_count_cs to serialize with lazy count initialization.
    {
        LOCK(m_count_cs);
        if (m_token_count_initialized.load(std::memory_order_acquire) && issue_undo_count > 0) {
            uint64_t current = m_token_count_cache.load();
            uint64_t to_sub = static_cast<uint64_t>(issue_undo_count);
            m_token_count_cache.store(current >= to_sub ? current - to_sub : 0);
        }
        if (m_transfer_count_initialized.load(std::memory_order_acquire) && txfr_undo_count > 0) {
            uint64_t current = m_transfer_count_cache.load();
            uint64_t to_sub = static_cast<uint64_t>(txfr_undo_count);
            m_transfer_count_cache.store(current >= to_sub ? current - to_sub : 0);
        }
    }
    
    if (undo_count > 0) {
        LogDebug(BCLog::TOKEN, "Disconnected %d token operations from block %d\n", 
                 undo_count, height);
    }
    
    return true;
}

uint256 TokenDB::GetBestBlock() const
{
    if (!m_db) return uint256();
    
    auto key = BestBlockKey();
    uint256 hash;
    if (!m_db->Read(key, hash)) {
        return uint256();
    }
    
    return hash;
}

bool TokenDB::SetBestBlock(const uint256& block_hash)
{
    if (!m_db) return false;
    
    auto key = BestBlockKey();
    m_db->Write(key, block_hash);
    return true;
}

uint64_t TokenDB::GetTokenCount() const
{
    // AUDIT FIX M-04: Use cached count to avoid full DB scan
    if (m_token_count_initialized.load(std::memory_order_acquire)) {
        return m_token_count_cache.load();
    }
    
    // AUDIT FIX [R13-03]: Lock during lazy initialization to prevent TOCTOU race.
    // Without this, a concurrent ProcessBlock could commit new tokens while the
    // scan is running.  ProcessBlock skips its fetch_add because initialized is
    // still false, and the scan might capture a stale LevelDB snapshot — resulting
    // in a permanent undercount until restart.
    LOCK(m_count_cs);
    // Double-check after acquiring lock (another thread may have initialized)
    if (m_token_count_initialized.load(std::memory_order_acquire)) {
        return m_token_count_cache.load();
    }

    // First call - perform the scan and cache the result
    uint64_t count = 0;
    if (!m_db) return count;
    
    std::unique_ptr<CDBIterator> cursor(m_db->NewIterator());
    cursor->Seek(db_prefix::TOKEN_INFO);
    
    // AUDIT FIX [R22-04]: Add MAX_TOTAL_ITERATIONS safety cap to prevent
    // unbounded iteration on corrupt DB entries.  Without this, corrupt
    // entries that fail GetKey() cause an infinite loop while holding
    // m_count_cs, blocking all threads that need the token count.
    static constexpr size_t MAX_TOTAL_ITERATIONS = 10000000;
    size_t total_iterations = 0;
    while (cursor->Valid()) {
        if (++total_iterations > MAX_TOTAL_ITERATIONS) {
            LogPrintf("WARNING: GetTokenCount scan hit iteration limit (%zu), count may be approximate\n", MAX_TOTAL_ITERATIONS);
            break;
        }
        TokenInfoKey key_obj{src20::TokenId{}};
        if (!cursor->GetKey(key_obj)) {
            cursor->Next();
            continue;
        }
        if (key_obj.prefix != db_prefix::TOKEN_INFO) {
            break;
        }
        count++;
        cursor->Next();
    }
    
    // Cache the result — store count before setting initialized flag
    m_token_count_cache.store(count);
    m_token_count_initialized.store(true, std::memory_order_release);
    
    return count;
}

uint64_t TokenDB::GetTransferCount() const
{
    // FIX L-04: Use cached transfer count to avoid full DB scan
    if (m_transfer_count_initialized.load(std::memory_order_acquire)) {
        return m_transfer_count_cache.load();
    }

    // AUDIT FIX [R13-03]: Lock during lazy initialization (see GetTokenCount).
    LOCK(m_count_cs);
    if (m_transfer_count_initialized.load(std::memory_order_acquire)) {
        return m_transfer_count_cache.load();
    }

    uint64_t count = 0;
    if (!m_db) return count;
    
    std::unique_ptr<CDBIterator> cursor(m_db->NewIterator());
    cursor->Seek(db_prefix::TRANSFER);
    
    // AUDIT FIX [R22-04]: Add MAX_TOTAL_ITERATIONS safety cap (see GetTokenCount).
    static constexpr size_t MAX_TOTAL_ITERATIONS = 10000000;
    size_t total_iterations = 0;
    while (cursor->Valid()) {
        if (++total_iterations > MAX_TOTAL_ITERATIONS) {
            LogPrintf("WARNING: GetTransferCount scan hit iteration limit (%zu), count may be approximate\n", MAX_TOTAL_ITERATIONS);
            break;
        }
        TransferKey key_obj;
        if (!cursor->GetKey(key_obj)) {
            cursor->Next();
            continue;
        }
        if (key_obj.prefix != db_prefix::TRANSFER) {
            break;
        }
        count++;
        cursor->Next();
    }
    
    // Cache the result — store count before setting initialized flag
    m_transfer_count_cache.store(count);
    m_transfer_count_initialized.store(true, std::memory_order_release);
    
    return count;
}

bool TokenDB::Sync()
{
    if (!m_db) return false;
    // Use WriteBatch with sync flag as Sync() method doesn't exist
    CDBBatch batch(*m_db);
    m_db->WriteBatch(batch, /*fSync=*/true);
    return true;
}

void TokenDB::Compact()
{
    // LevelDB compaction is automatic, but we can hint
    LogDebug(BCLog::TOKEN, "Compacting token database\n");
}

bool InitTokenDB(const fs::path& path, size_t cache_size_mb, bool wipe)
{
    try {
        // CRITICAL FIX [R6-01]: Pass `wipe` through so that -reindex and
        // -reindex-chainstate destroy the token database before replay.
        // Without this, blocks replay from genesis against stale final-state
        // data: ISSUE ops are rejected (ticker already exists), TRANSFER ops
        // read wrong balances, and the token DB becomes completely inconsistent.
        if (wipe) {
            LogPrintf("Token DB: wiping database for reindex at %s\n",
                      fs::PathToString(path));
        }
        g_tokendb = std::make_unique<TokenDB>(
            path, 
            cache_size_mb * 1024 * 1024,  // Convert to bytes
            false,  // Not memory-only
            wipe    // Wipe on -reindex / -reindex-chainstate
        );
        return g_tokendb->IsValid();
    } catch (const std::exception& e) {
        LogPrintf("Failed to initialize token database: %s\n", e.what());
        return false;
    }
}

void ShutdownTokenDB()
{
    if (g_tokendb) {
        // AUDIT FIX [R13-05]: Removed explicit Sync() call here — the
        // destructor (~TokenDB) already calls Sync() when m_db is valid.
        // Calling it twice is harmless but wastes a disk fsync.
        //
        // AUDIT FIX [R26-04]: Log at Info level (not Debug) so operators can
        // confirm token DB shutdown completed in their logs. This also serves
        // as a synchronization marker — if this message appears, no further
        // RPC or callback can be accessing g_tokendb (the shutdown sequence
        // in init.cpp drains all callbacks and takes cs_main before reaching
        // this point).
        LogPrintf("Shutting down token database...\n");
        g_tokendb.reset();
        LogPrintf("Token database shutdown complete.\n");
    }
}

} // namespace tokens
