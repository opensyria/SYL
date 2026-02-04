// Copyright (c) 2024-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <tokens/tokendb.h>

#include <hash.h>
#include <logging.h>
#include <primitives/transaction.h>
#include <script/src20.h>
#include <streams.h>
#include <util/fs.h>
#include <util/overflow.h>

namespace tokens {

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
    TransferKey() = default;
    TransferKey(const src20::TokenId& id, int h, const uint256& tx) : token_id(id), height(h), txid(tx) {}
    SERIALIZE_METHODS(TransferKey, obj) { READWRITE(obj.prefix, obj.token_id, obj.height, obj.txid); }
};

struct BlockTokensKey {
    uint8_t prefix{db_prefix::BLOCK_TOKENS};
    int height;
    explicit BlockTokensKey(int h) : height(h) {}
    SERIALIZE_METHODS(BlockTokensKey, obj) { READWRITE(obj.prefix, obj.height); }
};

struct UndoKey {
    uint8_t prefix{db_prefix::UNDO};
    int height;
    uint256 txid;
    UndoKey(int h, const uint256& tx) : height(h), txid(tx) {}
    SERIALIZE_METHODS(UndoKey, obj) { READWRITE(obj.prefix, obj.height, obj.txid); }
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
    
    // Check LRU cache first (updates access order)
    {
        LOCK(m_cs);
        auto cached = m_token_cache.get(token_id);
        if (cached) {
            info = *cached;
            return true;
        }
    }
    
    if (!m_db->Read(TokenInfoKey(token_id), info)) {
        return false;
    }
    
    UpdateCache(info);
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
    batch.Write(BalanceKey(address, token_id), balance);
    
    // Update holder index
    auto holder_key = TokenHoldersKey(token_id, address);
    if (balance > 0) {
        batch.Write(holder_key, true);
    } else {
        batch.Erase(holder_key);
    }
}

void TokenDB::WriteTokenInfoToBatch(CDBBatch& batch, const TokenInfo& info)
{
    // Write token info
    auto token_key = TokenInfoKey(info.token_id);
    batch.Write(token_key, info);
    
    // Write ticker index
    auto ticker_key = TickerKey(info.ticker);
    batch.Write(ticker_key, info.token_id);
    
    // Update cache
    UpdateCache(info);
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
    int64_t time)
{
    if (!m_db) return std::nullopt;
    
    // Check if ticker already exists
    if (TickerExists(issuance.ticker)) {
        LogDebug(BCLog::TOKEN, "Token ticker %s already exists\n", issuance.ticker);
        return std::nullopt;
    }
    
    // Generate token ID from transaction hash
    src20::TokenId token_id(Txid::FromUint256(txid));
    
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
    
    // AUDIT FIX: Use single atomic batch for ALL token registration data
    // Previously token info and balance were written separately, risking
    // orphaned tokens if the second write failed.
    CDBBatch batch(*m_db);
    
    // Write token info
    batch.Write(TokenInfoKey(info.token_id), info);
    batch.Write(TickerKey(info.ticker), info.token_id);
    
    // Credit initial supply to issuer
    batch.Write(BalanceKey(issuer_address, token_id), issuance.total_supply);
    batch.Write(TokenHoldersKey(token_id, issuer_address), true);
    
    // Update address token list
    std::set<src20::TokenId> addr_tokens;
    m_db->Read(AddrTokensKey(issuer_address), addr_tokens);
    addr_tokens.insert(token_id);
    batch.Write(AddrTokensKey(issuer_address), addr_tokens);
    
    // Record issuance as transfer from empty script (marks it as an issuance)
    TokenTransferRecord record;
    record.token_id = token_id;
    record.txid = txid;
    record.from_address = CScript();  // Empty = issuance
    record.to_address = issuer_address;
    record.amount = issuance.total_supply;
    record.height = height;
    record.time = time;
    
    batch.Write(TransferKey(token_id, height, txid), record);
    
    // Commit all atomically - WriteBatch throws dbwrapper_error on failure
    try {
        m_db->WriteBatch(batch);
    } catch (const dbwrapper_error& e) {
        LogPrintf("Failed to write token registration for %s: %s\n", issuance.ticker, e.what());
        return std::nullopt;
    }
    
    // Update cache AFTER successful write
    UpdateCache(info);
    
    // AUDIT FIX M-04: Update cached token count
    if (m_token_count_initialized.load()) {
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
    
    while (cursor->Valid() && result.size() < count) {
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

std::vector<TokenBalance> TokenDB::GetAddressBalances(const CScript& address) const
{
    std::vector<TokenBalance> result;
    if (!m_db) return result;
    
    // Read the address token list
    auto addr_key = AddrTokensKey(address);
    std::set<src20::TokenId> token_ids;
    if (!m_db->Read(addr_key, token_ids)) {
        return result;
    }
    
    for (const auto& token_id : token_ids) {
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
    
    while (cursor->Valid() && result.size() < count) {
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
    int64_t time)
{
    if (!m_db) return false;
    
    // Check sender balance
    uint64_t from_balance = GetBalance(from, token_id);
    if (from_balance < amount) {
        LogDebug(BCLog::TOKEN, "Insufficient token balance: have %llu, need %llu\n",
                 from_balance, amount);
        return false;
    }
    
    // Update balances
    uint64_t to_balance = GetBalance(to, token_id);
    
    // SECURITY: Check for overflow before adding to recipient balance
    auto new_to_balance = CheckedAdd(to_balance, amount);
    if (!new_to_balance) {
        LogDebug(BCLog::TOKEN, "Token transfer would cause balance overflow: %llu + %llu\n",
                 to_balance, amount);
        return false;
    }
    
    // FIX 3.2: Use atomic batch write for ALL balance updates
    // Previously, sender and recipient balances were written separately,
    // which could leave partial state on failure. Now all writes are atomic.
    CDBBatch batch(*m_db);
    
    // Write sender's new balance
    batch.Write(BalanceKey(from, token_id), from_balance - amount);
    
    // Update sender's holder index
    auto from_holder_key = TokenHoldersKey(token_id, from);
    if (from_balance == amount) {
        // Sender will have zero balance - remove from holder index
        batch.Erase(from_holder_key);
    }
    
    // Write recipient's new balance
    batch.Write(BalanceKey(to, token_id), *new_to_balance);
    
    // Update recipient's holder index
    auto to_holder_key = TokenHoldersKey(token_id, to);
    batch.Write(to_holder_key, true);
    
    // Add token to recipient's list if new holder
    if (to_balance == 0) {
        auto to_addr_key = AddrTokensKey(to);
        std::set<src20::TokenId> to_tokens;
        m_db->Read(to_addr_key, to_tokens);
        to_tokens.insert(token_id);
        batch.Write(to_addr_key, to_tokens);
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
    
    auto transfer_key = TransferKey(token_id, height, txid);
    batch.Write(transfer_key, record);
    
    // FIX 3.2: Include token stats update in the same atomic batch
    // Previously, stats were updated in a separate write which could
    // leave inconsistent state if the process crashed between writes.
    TokenInfo info;
    if (ReadTokenInfo(token_id, info)) {
        info.transfer_count++;
        
        // Update holder count
        if (to_balance == 0) info.holder_count++;
        if (from_balance == amount) info.holder_count--;
        
        // Write to batch instead of separate WriteTokenInfo call
        batch.Write(TokenInfoKey(info.token_id), info);
        batch.Write(TickerKey(info.ticker), info.token_id);
        UpdateCache(info);
    }
    
    m_db->WriteBatch(batch);
    
    // FIX L-04: Update cached transfer count
    if (m_transfer_count_initialized.load()) {
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
    int64_t time)
{
    if (!m_db) return false;
    
    // Check sender balance
    uint64_t from_balance = GetBalance(from, token_id);
    if (from_balance < amount) {
        LogDebug(BCLog::TOKEN, "Insufficient token balance for burn: have %llu, need %llu\n",
                 from_balance, amount);
        return false;
    }
    
    // Read token info for validation before modifying anything
    TokenInfo info;
    if (!ReadTokenInfo(token_id, info)) {
        LogDebug(BCLog::TOKEN, "Token burn failed: token %s not found\n",
                 token_id.ToString().substr(0, 16));
        return false;
    }
    
    // SECURITY FIX [M-01]: Check for circulating_supply underflow before modifying
    // This prevents wrap-around to astronomical values on database corruption
    if (info.circulating_supply < amount) {
        LogPrintf("ERROR: Token burn would cause circulating supply underflow for %s: supply=%llu, burn=%llu\n",
                  token_id.ToString().substr(0, 16), info.circulating_supply, amount);
        return false;
    }
    
    // SECURITY FIX [H-02]: Use atomic batch write for ALL updates
    // Previously, balance and token info were written separately, which could
    // leave partial state on crash. Now all writes are atomic.
    CDBBatch batch(*m_db);
    
    // Write sender's new balance
    WriteBalanceToBatch(batch, from, token_id, from_balance - amount);
    
    // Update holder index if sender now has zero balance
    if (from_balance == amount) {
        auto holder_key = TokenHoldersKey(token_id, from);
        batch.Erase(holder_key);
        
        // SECURITY FIX [M-01]: Check for holder_count underflow
        if (info.holder_count > 0) {
            info.holder_count--;
        }
    }
    
    // Update circulating supply (already checked for underflow above)
    info.circulating_supply -= amount;
    
    // Write token info to batch (atomic with balance update)
    WriteTokenInfoToBatch(batch, info);
    
    // Record burn as transfer to empty script (marks it as a burn for history)
    TokenTransferRecord record;
    record.token_id = token_id;
    record.txid = txid;
    record.from_address = from;
    record.to_address = CScript();  // Empty = burn
    record.amount = amount;
    record.height = height;
    record.time = time;
    
    auto transfer_key = TransferKey(token_id, height, txid);
    batch.Write(transfer_key, record);
    
    // Commit all changes atomically
    m_db->WriteBatch(batch);
    
    // FIX L-04: Update cached transfer count (burns are recorded as transfers too)
    if (m_transfer_count_initialized.load()) {
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
        // Get ALL tokens this address has ever interacted with (not just current balance)
        auto addr_key = AddrTokensKey(address);
        m_db->Read(addr_key, search_tokens);
    }
    
    // Search transfer records for each token
    for (const auto& tid : search_tokens) {
        std::unique_ptr<CDBIterator> cursor(m_db->NewIterator());
        
        auto start_key = TransferKey(tid, 0, uint256{});
        cursor->Seek(start_key);
        
        while (cursor->Valid() && result.size() < count) {
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
    
    auto start_key = TransferKey(token_id, 0, uint256{});
    cursor->Seek(start_key);
    
    while (cursor->Valid() && result.size() < count) {
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
    
    int ops_count = 0;
    int64_t block_time = block.GetBlockTime();
    std::vector<uint256> block_token_txs;
    CDBBatch batch(*m_db);
    
    // blockundo.vtxundo has entries for non-coinbase transactions (index i-1 for block.vtx[i])
    for (unsigned int i = 0; i < block.vtx.size(); i++) {
        const auto& tx = block.vtx[i];
        auto ops = src20::ParseTransactionSRC20(*tx);
        
        // Derive sender from first input's spent UTXO using blockundo
        CScript tx_sender;
        if (!tx->IsCoinBase() && !tx->vin.empty()) {
            // blockundo.vtxundo[i-1] contains undo for tx at block.vtx[i]
            const CTxUndo& txundo = blockundo.vtxundo[i - 1];
            // vprevout[0] is the first spent output (for vin[0])
            if (!txundo.vprevout.empty()) {
                tx_sender = txundo.vprevout[0].out.scriptPubKey;
            }
        }
        
        for (const auto& op : ops) {
            switch (op.action) {
                case src20::TokenAction::ISSUE: {
                    const auto* issuance = op.GetIssuance();
                    if (issuance && issuance->IsValid()) {
                        // Get issuer address from first non-OP_RETURN output
                        CScript issuer;
                        for (const auto& vout : tx->vout) {
                            if (!vout.scriptPubKey.IsUnspendable()) {
                                issuer = vout.scriptPubKey;
                                break;
                            }
                        }
                        
                        const uint256 txhash = tx->GetHash().ToUint256();
                        auto token_id_opt = RegisterToken(*issuance, txhash, issuer, height, block_time);
                        if (token_id_opt) {
                            // Create undo record for token issuance
                            TokenUndoRecord undo;
                            undo.op_type = TokenOpType::ISSUE;
                            undo.txid = txhash;
                            undo.token_id = *token_id_opt;
                            undo.ticker = issuance->ticker;
                            
                            auto undo_key = UndoKey(height, txhash);
                            batch.Write(undo_key, undo);
                            LogDebug(BCLog::TOKEN, "Created undo: %s\n", FormatUndoRecord(undo, height));
                            
                            block_token_txs.push_back(txhash);
                            ops_count++;
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
                            
                            // Get current balances for undo record
                            uint64_t prev_from_balance = GetBalance(sender, transfer->token_id);
                            uint64_t prev_to_balance = GetBalance(*recipient, transfer->token_id);
                            
                            auto token_info = GetTokenInfo(transfer->token_id);
                            uint64_t prev_holder_count = token_info ? token_info->holder_count : 0;
                            
                            const uint256 txhash = tx->GetHash().ToUint256();
                            if (TransferTokens(transfer->token_id, sender, *recipient,
                                             transfer->amount, txhash, height, block_time)) {
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
                                
                                auto undo_key = UndoKey(height, txhash);
                                batch.Write(undo_key, undo);
                                LogDebug(BCLog::TOKEN, "Created undo: %s\n", FormatUndoRecord(undo, height));
                                
                                block_token_txs.push_back(txhash);
                                ops_count++;
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
                        
                        // Get current state for undo
                        auto token_info = GetTokenInfo(burn->token_id);
                        uint64_t prev_circulating = token_info ? token_info->circulating_supply : 0;
                        uint64_t prev_balance = GetBalance(burner, burn->token_id);
                        
                        const uint256 txhash = tx->GetHash().ToUint256();
                        if (BurnTokens(burn->token_id, burner, burn->amount,
                                      txhash, height, block_time)) {
                            // Create undo record for burn
                            TokenUndoRecord undo;
                            undo.op_type = TokenOpType::BURN;
                            undo.txid = txhash;
                            undo.token_id = burn->token_id;
                            undo.from_address = burner;
                            undo.amount = burn->amount;
                            undo.prev_from_balance = prev_balance;
                            undo.prev_circulating_supply = prev_circulating;
                            
                            auto undo_key = UndoKey(height, txhash);
                            batch.Write(undo_key, undo);
                            LogDebug(BCLog::TOKEN, "Created undo: %s\n", FormatUndoRecord(undo, height));
                            
                            block_token_txs.push_back(txhash);
                            ops_count++;
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
    if (!block_token_txs.empty()) {
        auto key = BlockTokensKey(height);
        batch.Write(key, block_token_txs);
    }
    
    // Write all data atomically - WriteBatch throws dbwrapper_error on failure
    // If exception is thrown, SetBestBlock won't be called, keeping state consistent
    try {
        m_db->WriteBatch(batch);
    } catch (const dbwrapper_error& e) {
        LogPrintf("CRITICAL: Failed to write token operations for block %d: %s\n", height, e.what());
        throw;  // Re-throw to signal block processing failure
    }
    
    // Update best block only on successful write
    SetBestBlock(block.GetHash());
    
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
    
    int ops_count = 0;
    int64_t block_time = block.GetBlockTime();
    std::vector<uint256> block_token_txs;
    CDBBatch batch(*m_db);
    
    for (const auto& tx : block.vtx) {
        auto ops = src20::ParseTransactionSRC20(*tx);
        
        // Derive sender from first input's spent UTXO
        CScript tx_sender;
        if (view && !tx->vin.empty() && !tx->IsCoinBase()) {
            const COutPoint& prevout = tx->vin[0].prevout;
            const Coin& coin = view->AccessCoin(prevout);
            if (!coin.IsSpent()) {
                tx_sender = coin.out.scriptPubKey;
            }
        }
        
        for (const auto& op : ops) {
            switch (op.action) {
                case src20::TokenAction::ISSUE: {
                    const auto* issuance = op.GetIssuance();
                    if (issuance && issuance->IsValid()) {
                        // Get issuer address from first non-OP_RETURN output
                        CScript issuer;
                        for (const auto& vout : tx->vout) {
                            if (!vout.scriptPubKey.IsUnspendable()) {
                                issuer = vout.scriptPubKey;
                                break;
                            }
                        }
                        
                        const uint256 txhash = tx->GetHash().ToUint256();
                        auto token_id_opt = RegisterToken(*issuance, txhash, issuer, height, block_time);
                        if (token_id_opt) {
                            // Create undo record for token issuance
                            TokenUndoRecord undo;
                            undo.op_type = TokenOpType::ISSUE;
                            undo.txid = txhash;
                            undo.token_id = *token_id_opt;
                            undo.ticker = issuance->ticker;
                            
                            auto undo_key = UndoKey(height, txhash);
                            batch.Write(undo_key, undo);
                            LogDebug(BCLog::TOKEN, "Created undo: %s\n", FormatUndoRecord(undo, height));
                            
                            block_token_txs.push_back(txhash);
                            ops_count++;
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
                            
                            // Get current balances for undo record
                            uint64_t prev_from_balance = GetBalance(sender, transfer->token_id);
                            uint64_t prev_to_balance = GetBalance(*recipient, transfer->token_id);
                            
                            auto token_info = GetTokenInfo(transfer->token_id);
                            uint64_t prev_holder_count = token_info ? token_info->holder_count : 0;
                            
                            const uint256 txhash = tx->GetHash().ToUint256();
                            if (TransferTokens(transfer->token_id, sender, *recipient,
                                             transfer->amount, txhash, height, block_time)) {
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
                                
                                auto undo_key = UndoKey(height, txhash);
                                batch.Write(undo_key, undo);
                                LogDebug(BCLog::TOKEN, "Created undo: %s\n", FormatUndoRecord(undo, height));
                                
                                block_token_txs.push_back(txhash);
                                ops_count++;
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
                        
                        // Get current state for undo
                        auto token_info = GetTokenInfo(burn->token_id);
                        uint64_t prev_circulating = token_info ? token_info->circulating_supply : 0;
                        uint64_t prev_balance = GetBalance(burner, burn->token_id);
                        
                        const uint256 txhash = tx->GetHash().ToUint256();
                        if (BurnTokens(burn->token_id, burner, burn->amount,
                                      txhash, height, block_time)) {
                            // Create undo record for burn
                            TokenUndoRecord undo;
                            undo.op_type = TokenOpType::BURN;
                            undo.txid = txhash;
                            undo.token_id = burn->token_id;
                            undo.from_address = burner;
                            undo.amount = burn->amount;
                            undo.prev_from_balance = prev_balance;
                            undo.prev_circulating_supply = prev_circulating;
                            
                            auto undo_key = UndoKey(height, txhash);
                            batch.Write(undo_key, undo);
                            LogDebug(BCLog::TOKEN, "Created undo: %s\n", FormatUndoRecord(undo, height));
                            
                            block_token_txs.push_back(txhash);
                            ops_count++;
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
    if (!block_token_txs.empty()) {
        auto key = BlockTokensKey(height);
        batch.Write(key, block_token_txs);
    }
    
    // Write all undo data atomically - WriteBatch throws dbwrapper_error on failure
    try {
        m_db->WriteBatch(batch);
    } catch (const dbwrapper_error& e) {
        LogPrintf("CRITICAL: Failed to write token operations for block %d: %s\n", height, e.what());
        throw;  // Re-throw to signal block processing failure
    }
    
    // Update best block only on successful write
    SetBestBlock(block.GetHash());
    
    if (ops_count > 0) {
        LogDebug(BCLog::TOKEN, "Processed %d token operations in block %d\n", ops_count, height);
    }
    
    return ops_count;
}

bool TokenDB::DisconnectBlock(const CBlock& block, int height)
{
    if (!m_db) return false;
    
    // Read the token transactions from this block
    auto key = BlockTokensKey(height);
    std::vector<uint256> block_token_txs;
    if (!m_db->Read(key, block_token_txs)) {
        // No token transactions in this block
        return true;
    }
    
    CDBBatch batch(*m_db);
    int undo_count = 0;
    
    // Process transactions in reverse order to properly undo
    for (auto it = block_token_txs.rbegin(); it != block_token_txs.rend(); ++it) {
        const uint256& txid = *it;
        
        // Read the undo record
        auto undo_key = UndoKey(height, txid);
        TokenUndoRecord undo;
        if (!m_db->Read(undo_key, undo)) {
            LogPrintf("Warning: No undo record for token tx %s at height %d\n",
                     txid.ToString().substr(0, 16), height);
            continue;
        }
        
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
                
                // Invalidate cache
                InvalidateCache(undo.token_id);
                
                // AUDIT FIX M-04: Update cached token count
                if (m_token_count_initialized.load() && m_token_count_cache.load() > 0) {
                    m_token_count_cache.fetch_sub(1);
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
                
                // Restore recipient's balance (to batch for atomicity)
                WriteBalanceToBatch(batch, undo.to_address, undo.token_id, undo.prev_to_balance);
                
                // Restore holder count and transfer count (to batch)
                auto token_info = GetTokenInfo(undo.token_id);
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
                    WriteTokenInfoToBatch(batch, *token_info);
                }
                
                // Delete transfer record
                auto transfer_key = TransferKey(undo.token_id, height, txid);
                batch.Erase(transfer_key);
                
                // FIX L-04: Update cached transfer count
                if (m_transfer_count_initialized.load() && m_transfer_count_cache.load() > 0) {
                    m_transfer_count_cache.fetch_sub(1);
                }
                
                InvalidateCache(undo.token_id);
                undo_count++;
                break;
            }
            
            case TokenOpType::BURN: {
                // Restore burned tokens
                LogDebug(BCLog::TOKEN, "Undoing token burn: %s amount=%lu\n",
                        undo.token_id.ToString().substr(0, 16), undo.amount);
                
                // Restore burner's balance (to batch for atomicity)
                WriteBalanceToBatch(batch, undo.from_address, undo.token_id, undo.prev_from_balance);
                
                // Restore circulating supply (to batch for atomicity)
                auto token_info = GetTokenInfo(undo.token_id);
                if (token_info) {
                    token_info->circulating_supply = undo.prev_circulating_supply;
                    WriteTokenInfoToBatch(batch, *token_info);
                }
                
                // FIX L-04: Update cached transfer count (burns are recorded as transfers)
                if (m_transfer_count_initialized.load() && m_transfer_count_cache.load() > 0) {
                    m_transfer_count_cache.fetch_sub(1);
                }
                
                InvalidateCache(undo.token_id);
                undo_count++;
                break;
            }
            
            default:
                LogPrintf("Warning: Unknown undo operation type in tx %s\n",
                         txid.ToString().substr(0, 16));
                break;
        }
        
        // Delete the undo record
        batch.Erase(undo_key);
    }
    
    // Remove block record
    batch.Erase(key);
    
    // Write all changes atomically
    m_db->WriteBatch(batch);
    
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
    if (m_token_count_initialized.load()) {
        return m_token_count_cache.load();
    }
    
    // First call - perform the scan and cache the result
    uint64_t count = 0;
    if (!m_db) return count;
    
    std::unique_ptr<CDBIterator> cursor(m_db->NewIterator());
    cursor->Seek(db_prefix::TOKEN_INFO);
    
    while (cursor->Valid()) {
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
    
    // Cache the result
    m_token_count_cache.store(count);
    m_token_count_initialized.store(true);
    
    return count;
}

uint64_t TokenDB::GetTransferCount() const
{
    // FIX L-04: Use cached transfer count to avoid full DB scan
    if (m_transfer_count_initialized.load()) {
        return m_transfer_count_cache.load();
    }

    uint64_t count = 0;
    if (!m_db) return count;
    
    std::unique_ptr<CDBIterator> cursor(m_db->NewIterator());
    cursor->Seek(db_prefix::TRANSFER);
    
    while (cursor->Valid()) {
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
    
    // Cache the result
    m_transfer_count_cache.store(count);
    m_transfer_count_initialized.store(true);
    
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

bool InitTokenDB(const fs::path& path, size_t cache_size_mb)
{
    try {
        g_tokendb = std::make_unique<TokenDB>(
            path, 
            cache_size_mb * 1024 * 1024,  // Convert to bytes
            false,  // Not memory-only
            false   // Don't wipe
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
        g_tokendb->Sync();
        g_tokendb.reset();
        LogDebug(BCLog::TOKEN, "Token database shutdown complete\n");
    }
}

} // namespace tokens
