// Copyright (c) 2024-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <tokens/tokenvalidation.h>

#include <logging.h>
#include <tinyformat.h>

#include <limits>

namespace tokens {

std::unique_ptr<MempoolTokenState> g_mempool_tokens;

std::string TokenValidationResultToString(TokenValidationResult result)
{
    switch (result) {
        case TokenValidationResult::OK:
            return "OK";
        case TokenValidationResult::INVALID_FORMAT:
            return "Invalid SRC-20 data format";
        case TokenValidationResult::INVALID_VERSION:
            return "Unsupported protocol version";
        case TokenValidationResult::INVALID_ACTION:
            return "Unknown action type";
        case TokenValidationResult::INVALID_TICKER:
            return "Invalid ticker format";
        case TokenValidationResult::RESERVED_TICKER:
            return "Ticker is reserved";
        case TokenValidationResult::DUPLICATE_TICKER:
            return "Ticker already exists";
        case TokenValidationResult::TOKEN_NOT_FOUND:
            return "Token ID not found";
        case TokenValidationResult::INSUFFICIENT_BALANCE:
            return "Insufficient token balance";
        case TokenValidationResult::INVALID_AMOUNT:
            return "Invalid amount";
        case TokenValidationResult::MISSING_RECIPIENT:
            return "Transfer missing recipient output";
        case TokenValidationResult::BLOCK_TOKEN_LIMIT:
            return "Block token operation limit exceeded";
        case TokenValidationResult::INTERNAL_ERROR:
            return "Internal error";
    }
    return "Unknown error";
}

// TokenValidator implementation

TokenValidation TokenValidator::ValidateIssuance(const src20::TokenIssuance& issuance) const
{
    // Check ticker format
    if (issuance.ticker.empty() || issuance.ticker.size() > src20::MAX_TICKER_LENGTH) {
        return TokenValidation(TokenValidationResult::INVALID_TICKER,
                              strprintf("Ticker must be 1-%d characters", src20::MAX_TICKER_LENGTH));
    }

    for (char c : issuance.ticker) {
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
            return TokenValidation(TokenValidationResult::INVALID_TICKER,
                                  "Ticker must be uppercase alphanumeric");
        }
    }

    // Check reserved tickers
    if (src20::reserved::IsReservedTicker(issuance.ticker)) {
        return TokenValidation(TokenValidationResult::RESERVED_TICKER,
                              strprintf("Ticker '%s' is reserved", issuance.ticker));
    }

    // Check if ticker already exists
    if (m_tokendb.TickerExists(issuance.ticker)) {
        return TokenValidation(TokenValidationResult::DUPLICATE_TICKER,
                              strprintf("Ticker '%s' already exists", issuance.ticker));
    }

    // Check name
    if (issuance.name.empty() || issuance.name.size() > src20::MAX_NAME_LENGTH) {
        return TokenValidation(TokenValidationResult::INVALID_FORMAT,
                              strprintf("Name must be 1-%d characters", src20::MAX_NAME_LENGTH));
    }

    // SECURITY FIX L-01: Validate token name characters
    // Prevent special characters that could cause display issues, XSS, or parsing problems
    // Allowed: alphanumeric, spaces, hyphens, periods, parentheses
    for (size_t i = 0; i < issuance.name.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(issuance.name[i]);
        bool valid = 
            (c >= 'A' && c <= 'Z') ||      // Uppercase
            (c >= 'a' && c <= 'z') ||      // Lowercase
            (c >= '0' && c <= '9') ||      // Digits
            c == ' ' ||                    // Spaces
            c == '-' ||                    // Hyphens
            c == '.' ||                    // Periods
            c == '(' || c == ')';          // Parentheses
        
        if (!valid) {
            return TokenValidation(TokenValidationResult::INVALID_FORMAT,
                                  strprintf("Token name contains invalid character at position %zu (char code: %d)", i, c));
        }
        
        // Prevent control characters (0x00-0x1F, 0x7F)
        if (c < 0x20 || c == 0x7F) {
            return TokenValidation(TokenValidationResult::INVALID_FORMAT,
                                  "Token name cannot contain control characters");
        }
    }
    
    // Prevent names that are only whitespace
    bool hasNonSpace = false;
    for (char c : issuance.name) {
        if (c != ' ') {
            hasNonSpace = true;
            break;
        }
    }
    if (!hasNonSpace) {
        return TokenValidation(TokenValidationResult::INVALID_FORMAT,
                              "Token name cannot be only whitespace");
    }

    // AUDIT FIX M-01: Reject leading/trailing whitespace (could confuse users)
    if (!issuance.name.empty()) {
        if (issuance.name.front() == ' ') {
            return TokenValidation(TokenValidationResult::INVALID_FORMAT,
                                  "Token name cannot have leading spaces");
        }
        if (issuance.name.back() == ' ') {
            return TokenValidation(TokenValidationResult::INVALID_FORMAT,
                                  "Token name cannot have trailing spaces");
        }
    }

    // AUDIT FIX M-01: Reject consecutive spaces (visually misleading)
    if (issuance.name.find("  ") != std::string::npos) {
        return TokenValidation(TokenValidationResult::INVALID_FORMAT,
                              "Token name cannot have consecutive spaces");
    }

    // Check decimals
    if (issuance.decimals > src20::MAX_DECIMALS) {
        return TokenValidation(TokenValidationResult::INVALID_FORMAT,
                              strprintf("Decimals must be 0-%d", src20::MAX_DECIMALS));
    }

    // Check supply
    if (issuance.total_supply == 0) {
        return TokenValidation(TokenValidationResult::INVALID_AMOUNT,
                              "Supply must be greater than zero");
    }

    return TokenValidation(TokenValidationResult::OK);
}

TokenValidation TokenValidator::ValidateTransfer(
    const src20::TokenTransfer& transfer,
    const CTransaction& tx,
    const CScript& sender) const
{
    // Check amount
    if (transfer.amount == 0) {
        return TokenValidation(TokenValidationResult::INVALID_AMOUNT,
                              "Transfer amount must be greater than zero");
    }

    // Check token exists
    if (!m_tokendb.TokenExists(transfer.token_id)) {
        return TokenValidation(TokenValidationResult::TOKEN_NOT_FOUND,
                              strprintf("Token %s not found", transfer.token_id.ToString().substr(0, 16)));
    }

    // Check recipient output exists
    auto recipient = src20::GetTransferRecipient(tx);
    if (!recipient) {
        return TokenValidation(TokenValidationResult::MISSING_RECIPIENT,
                              "Transfer must have recipient in output 1");
    }

    // Check sender has sufficient balance
    uint64_t balance = m_tokendb.GetBalance(sender, transfer.token_id);
    if (balance < transfer.amount) {
        return TokenValidation(TokenValidationResult::INSUFFICIENT_BALANCE,
                              strprintf("Need %llu, have %llu", transfer.amount, balance));
    }

    return TokenValidation(TokenValidationResult::OK);
}

TokenValidation TokenValidator::ValidateBurn(
    const src20::TokenBurn& burn,
    const CScript& sender) const
{
    // Check amount
    if (burn.amount == 0) {
        return TokenValidation(TokenValidationResult::INVALID_AMOUNT,
                              "Burn amount must be greater than zero");
    }

    // Check token exists
    if (!m_tokendb.TokenExists(burn.token_id)) {
        return TokenValidation(TokenValidationResult::TOKEN_NOT_FOUND,
                              strprintf("Token %s not found", burn.token_id.ToString().substr(0, 16)));
    }

    // Check sender has sufficient balance
    uint64_t balance = m_tokendb.GetBalance(sender, burn.token_id);
    if (balance < burn.amount) {
        return TokenValidation(TokenValidationResult::INSUFFICIENT_BALANCE,
                              strprintf("Need %llu to burn, have %llu", burn.amount, balance));
    }

    return TokenValidation(TokenValidationResult::OK);
}

TokenValidation TokenValidator::ValidateOperation(
    const src20::SRC20Operation& op,
    const CTransaction& tx,
    const CScript& sender) const
{
    if (!op.IsValid()) {
        return TokenValidation(TokenValidationResult::INVALID_FORMAT);
    }

    TokenValidation result;

    switch (op.action) {
        case src20::TokenAction::ISSUE:
            if (const auto* issuance = op.GetIssuance()) {
                result = ValidateIssuance(*issuance);
            } else {
                result = TokenValidation(TokenValidationResult::INVALID_FORMAT);
            }
            break;

        case src20::TokenAction::TRANSFER:
            if (const auto* transfer = op.GetTransfer()) {
                result = ValidateTransfer(*transfer, tx, sender);
            } else {
                result = TokenValidation(TokenValidationResult::INVALID_FORMAT);
            }
            break;

        case src20::TokenAction::BURN:
            if (const auto* burn = op.GetBurn()) {
                result = ValidateBurn(*burn, sender);
            } else {
                result = TokenValidation(TokenValidationResult::INVALID_FORMAT);
            }
            break;

        default:
            result = TokenValidation(TokenValidationResult::INVALID_ACTION);
            break;
    }

    if (result.IsValid()) {
        result.operation = op;
    }

    return result;
}

std::vector<TokenValidation> TokenValidator::ValidateTransaction(
    const CTransaction& tx,
    const CScript& sender) const
{
    std::vector<TokenValidation> results;

    auto ops = src20::ParseTransactionSRC20(tx);
    for (const auto& op : ops) {
        results.push_back(ValidateOperation(op, tx, sender));
    }

    return results;
}

TokenValidation TokenValidator::ValidateBlock(const CBlock& block, int height) const
{
    // Check block token limit
    size_t token_op_count = 0;
    std::set<std::string> block_tickers;  // Track tickers issued in this block

    for (const auto& tx : block.vtx) {
        auto ops = src20::ParseTransactionSRC20(*tx);
        token_op_count += ops.size();

        // Check for duplicate tickers within the block
        for (const auto& op : ops) {
            if (op.action == src20::TokenAction::ISSUE) {
                const auto* issuance = op.GetIssuance();
                if (issuance) {
                    if (block_tickers.count(issuance->ticker)) {
                        // Duplicate ticker in same block - second one is silently ignored
                        // This is soft-fork behavior: block remains valid but duplicate issuance has no effect
                        // AUDIT FIX: Add logging so operators can see skipped duplicates
                        LogDebug(BCLog::TOKEN, "Skipping duplicate ticker '%s' in block at height %d (tx: %s)\n",
                                 issuance->ticker, height, tx->GetHash().ToString().substr(0, 16));
                        continue;
                    }
                    block_tickers.insert(issuance->ticker);
                }
            }
        }
    }

    // Check spam limit
    if (token_op_count > src20::MAX_TOKENS_PER_BLOCK) {
        return TokenValidation(TokenValidationResult::BLOCK_TOKEN_LIMIT,
                              strprintf("Block has %zu token ops, max is %zu",
                                       token_op_count, src20::MAX_TOKENS_PER_BLOCK));
    }

    return TokenValidation(TokenValidationResult::OK);
}

bool TokenValidator::CheckMempoolAccept(const CTransaction& tx, const CScript& sender) const
{
    auto validations = ValidateTransaction(tx, sender);
    for (const auto& v : validations) {
        if (!v.IsValid()) {
            LogDebug(BCLog::MEMPOOL, "Token validation failed: %s\n", v.message);
            return false;
        }
    }
    return true;
}

// MempoolTokenState implementation

bool MempoolTokenState::AddTransaction(const CTransaction& tx, const CScript& sender)
{
    LOCK(m_cs);

    auto ops = src20::ParseTransactionSRC20(tx);
    if (ops.empty()) {
        return true;  // No token operations, nothing to track
    }

    // DoS protection checks (already holds lock, call internal versions)
    if (m_tx_ops.size() >= MAX_TOTAL_PENDING_OPS) {
        LogDebug(BCLog::MEMPOOL, "Token mempool full, rejecting tx\n");
        return false;
    }
    
    auto addr_it = m_ops_per_address.find(sender);
    size_t current_ops = addr_it != m_ops_per_address.end() ? addr_it->second : 0;
    if (current_ops + ops.size() > MAX_PENDING_OPS_PER_ADDRESS) {
        LogDebug(BCLog::MEMPOOL, "Too many pending token ops for address\n");
        return false;
    }
    
    int64_t now = GetTime();
    if (IsRateLimited(sender, now)) {
        LogDebug(BCLog::MEMPOOL, "Address rate limited for token ops\n");
        return false;
    }

    const uint256 txid = tx.GetHash().ToUint256();
    
    // FIX 4.1: Track per-transaction balance deltas for proper reversal
    TxBalanceDeltas tx_deltas;

    for (const auto& op : ops) {
        switch (op.action) {
            case src20::TokenAction::ISSUE: {
                const auto* issuance = op.GetIssuance();
                if (issuance) {
                    // Check if ticker is already pending
                    if (m_pending_tickers.count(issuance->ticker)) {
                        LogPrintf("Ticker %s already pending in mempool\n",
                                 issuance->ticker.c_str());
                        return false;
                    }
                    m_pending_tickers[issuance->ticker] = txid;
                }
                break;
            }

            case src20::TokenAction::TRANSFER: {
                const auto* transfer = op.GetTransfer();
                if (transfer) {
                    // AUDIT FIX [L-08]: Check for int64_t overflow before modifying pending balances.
                    // transfer->amount is uint64_t; casting to int64_t could overflow if > INT64_MAX.
                    if (transfer->amount > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                        LogDebug(BCLog::MEMPOOL, "Token transfer amount exceeds int64_t range\n");
                        break;
                    }
                    int64_t signed_amount = static_cast<int64_t>(transfer->amount);
                    
                    // Debit sender (check underflow)
                    int64_t& sender_delta = m_pending_balances[sender][transfer->token_id];
                    if (sender_delta < std::numeric_limits<int64_t>::min() + signed_amount) {
                        LogDebug(BCLog::MEMPOOL, "Pending balance underflow for sender\n");
                        break;
                    }
                    sender_delta -= signed_amount;
                    tx_deltas.deltas[sender][transfer->token_id] -= signed_amount;

                    // Credit recipient (check overflow)
                    auto recipient = src20::GetTransferRecipient(tx);
                    if (recipient) {
                        int64_t& recip_delta = m_pending_balances[*recipient][transfer->token_id];
                        if (recip_delta > std::numeric_limits<int64_t>::max() - signed_amount) {
                            LogDebug(BCLog::MEMPOOL, "Pending balance overflow for recipient\n");
                            // Undo sender debit
                            sender_delta += signed_amount;
                            break;
                        }
                        recip_delta += signed_amount;
                        tx_deltas.deltas[*recipient][transfer->token_id] += signed_amount;
                    }
                }
                break;
            }

            case src20::TokenAction::BURN: {
                const auto* burn = op.GetBurn();
                if (burn) {
                    // AUDIT FIX [L-08]: Overflow check for burn amount
                    if (burn->amount > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                        LogDebug(BCLog::MEMPOOL, "Token burn amount exceeds int64_t range\n");
                        break;
                    }
                    int64_t signed_amount = static_cast<int64_t>(burn->amount);
                    int64_t& sender_delta = m_pending_balances[sender][burn->token_id];
                    if (sender_delta < std::numeric_limits<int64_t>::min() + signed_amount) {
                        LogDebug(BCLog::MEMPOOL, "Pending balance underflow for burn\n");
                        break;
                    }
                    sender_delta -= signed_amount;
                    tx_deltas.deltas[sender][burn->token_id] -= signed_amount;
                }
                break;
            }

            default:
                break;
        }
    }

    m_tx_ops[txid] = ops;
    m_tx_balance_deltas[txid] = std::move(tx_deltas);
    m_tx_sender[txid] = sender;  // Track sender for cleanup
    
    // Update DoS tracking
    m_ops_per_address[sender] += ops.size();
    RecordOperation(sender, now);
    
    return true;
}

void MempoolTokenState::RemoveTransaction(const uint256& txid)
{
    LOCK(m_cs);

    auto it = m_tx_ops.find(txid);
    if (it == m_tx_ops.end()) {
        return;
    }

    // Remove pending tickers
    for (const auto& op : it->second) {
        if (op.action == src20::TokenAction::ISSUE) {
            const auto* issuance = op.GetIssuance();
            if (issuance) {
                m_pending_tickers.erase(issuance->ticker);
            }
        }
    }

    // FIX 4.1: Properly reverse balance changes using stored per-tx deltas
    // This prevents mempool state drift that could allow double-spend attempts
    auto deltas_it = m_tx_balance_deltas.find(txid);
    if (deltas_it != m_tx_balance_deltas.end()) {
        for (const auto& [address, token_deltas] : deltas_it->second.deltas) {
            for (const auto& [token_id, delta] : token_deltas) {
                // Reverse the delta by subtracting it
                m_pending_balances[address][token_id] -= delta;
                
                // Clean up zero entries to prevent memory bloat
                if (m_pending_balances[address][token_id] == 0) {
                    m_pending_balances[address].erase(token_id);
                }
            }
            // Clean up empty address entries
            if (m_pending_balances[address].empty()) {
                m_pending_balances.erase(address);
            }
        }
        m_tx_balance_deltas.erase(deltas_it);
    }

    // Decrement DoS tracking for sender
    auto sender_it = m_tx_sender.find(txid);
    if (sender_it != m_tx_sender.end()) {
        auto& count = m_ops_per_address[sender_it->second];
        if (count >= it->second.size()) {
            count -= it->second.size();
        } else {
            count = 0;
        }
        // Clean up zero entries
        if (count == 0) {
            m_ops_per_address.erase(sender_it->second);
        }
        m_tx_sender.erase(sender_it);
    }

    m_tx_ops.erase(it);
}

bool MempoolTokenState::IsTickerPending(const std::string& ticker) const
{
    LOCK(m_cs);
    return m_pending_tickers.count(ticker) > 0;
}

int64_t MempoolTokenState::GetPendingBalanceDelta(
    const CScript& address,
    const src20::TokenId& token_id) const
{
    LOCK(m_cs);

    auto addr_it = m_pending_balances.find(address);
    if (addr_it == m_pending_balances.end()) {
        return 0;
    }

    auto token_it = addr_it->second.find(token_id);
    if (token_it == addr_it->second.end()) {
        return 0;
    }

    return token_it->second;
}

uint64_t MempoolTokenState::GetEffectiveBalance(
    const TokenDB& tokendb,
    const CScript& address,
    const src20::TokenId& token_id) const
{
    uint64_t confirmed = tokendb.GetBalance(address, token_id);
    int64_t pending = GetPendingBalanceDelta(address, token_id);

    // AUDIT FIX [L-07]: Use branch-based arithmetic to avoid signed overflow UB
    // Previously, casting confirmed to int64_t could cause undefined behavior
    // if confirmed > INT64_MAX (~9.2 quintillion).
    if (pending < 0) {
        // Subtract absolute value of negative pending
        uint64_t abs_pending = static_cast<uint64_t>(-pending);
        return confirmed > abs_pending ? confirmed - abs_pending : 0;
    } else {
        // Add positive pending with overflow check
        uint64_t pos_pending = static_cast<uint64_t>(pending);
        if (confirmed > UINT64_MAX - pos_pending) {
            return UINT64_MAX;  // Saturate on overflow
        }
        return confirmed + pos_pending;
    }
}

bool MempoolTokenState::CanTransfer(
    const TokenDB& tokendb,
    const CScript& from,
    const src20::TokenId& token_id,
    uint64_t amount) const
{
    uint64_t effective = GetEffectiveBalance(tokendb, from, token_id);
    return effective >= amount;
}

void MempoolTokenState::Clear()
{
    LOCK(m_cs);
    m_pending_tickers.clear();
    m_pending_balances.clear();
    m_tx_ops.clear();
    m_tx_balance_deltas.clear();
    m_tx_sender.clear();
    m_ops_per_address.clear();
    // Note: Don't clear rate limit timestamps on block - they should persist
}

size_t MempoolTokenState::GetPendingCount() const
{
    LOCK(m_cs);
    return m_tx_ops.size();
}

size_t MempoolTokenState::GetPendingOpsForAddress(const CScript& address) const
{
    LOCK(m_cs);
    auto it = m_ops_per_address.find(address);
    return it != m_ops_per_address.end() ? it->second : 0;
}

bool MempoolTokenState::IsRateLimited(const CScript& address, int64_t now) const
{
    auto it = m_rate_limit_timestamps.find(address);
    if (it == m_rate_limit_timestamps.end()) {
        return false;
    }
    
    // Count operations in the time window
    size_t ops_in_window = 0;
    const int64_t window_start = now - RATE_LIMIT_WINDOW_SECONDS;
    
    for (int64_t ts : it->second) {
        if (ts >= window_start) {
            ++ops_in_window;
        }
    }
    
    return ops_in_window >= MAX_OPS_PER_ADDRESS_PER_WINDOW;
}

void MempoolTokenState::RecordOperation(const CScript& address, int64_t now)
{
    auto& timestamps = m_rate_limit_timestamps[address];
    
    // Prune old timestamps
    const int64_t window_start = now - RATE_LIMIT_WINDOW_SECONDS;
    timestamps.erase(
        std::remove_if(timestamps.begin(), timestamps.end(),
                      [window_start](int64_t ts) { return ts < window_start; }),
        timestamps.end()
    );
    
    // Add current timestamp
    timestamps.push_back(now);
}

std::string MempoolTokenState::CheckDoSLimits(const CTransaction& tx, const CScript& sender) const
{
    LOCK(m_cs);
    
    auto ops = src20::ParseTransactionSRC20(tx);
    if (ops.empty()) {
        return "";  // No token operations, no DoS concern
    }
    
    // Check total pending operations limit
    if (m_tx_ops.size() >= MAX_TOTAL_PENDING_OPS) {
        return strprintf("Token mempool full: %d pending operations (max %d)",
                        m_tx_ops.size(), MAX_TOTAL_PENDING_OPS);
    }
    
    // Check per-address pending operations limit
    auto addr_it = m_ops_per_address.find(sender);
    size_t current_ops = addr_it != m_ops_per_address.end() ? addr_it->second : 0;
    
    if (current_ops + ops.size() > MAX_PENDING_OPS_PER_ADDRESS) {
        return strprintf("Too many pending token operations for address: %d (max %d)",
                        current_ops, MAX_PENDING_OPS_PER_ADDRESS);
    }
    
    // Check pending issuances limit
    size_t issuance_count = 0;
    for (const auto& op : ops) {
        if (op.action == src20::TokenAction::ISSUE) {
            ++issuance_count;
        }
    }
    
    if (issuance_count > 0 && m_pending_tickers.size() + issuance_count > MAX_PENDING_ISSUANCES) {
        return strprintf("Too many pending token issuances: %d (max %d)",
                        m_pending_tickers.size(), MAX_PENDING_ISSUANCES);
    }
    
    // Check rate limiting
    int64_t now = GetTime();
    if (IsRateLimited(sender, now)) {
        return strprintf("Address rate limited: too many token operations in %d seconds",
                        RATE_LIMIT_WINDOW_SECONDS);
    }
    
    return "";  // All checks passed
}

size_t MempoolTokenState::PruneStaleRateLimitEntries()
{
    LOCK(m_cs);
    
    int64_t now = GetTime();
    const int64_t window_start = now - RATE_LIMIT_WINDOW_SECONDS;
    size_t pruned = 0;
    
    // Iterate and remove entries with no recent timestamps
    for (auto it = m_rate_limit_timestamps.begin(); it != m_rate_limit_timestamps.end(); ) {
        // Remove old timestamps from this address
        auto& timestamps = it->second;
        timestamps.erase(
            std::remove_if(timestamps.begin(), timestamps.end(),
                          [window_start](int64_t ts) { return ts < window_start; }),
            timestamps.end()
        );
        
        // If no recent timestamps, remove the entire address entry
        if (timestamps.empty()) {
            it = m_rate_limit_timestamps.erase(it);
            ++pruned;
        } else {
            ++it;
        }
    }
    
    if (pruned > 0) {
        LogDebug(BCLog::TOKEN, "Pruned %zu stale rate limit entries\n", pruned);
    }
    
    return pruned;
}

// ConsensusTokenValidator implementation

size_t ConsensusTokenValidator::CountTokenOperations(const CBlock& block)
{
    size_t count = 0;
    for (const auto& tx : block.vtx) {
        count += src20::ParseTransactionSRC20(*tx).size();
    }
    return count;
}

bool ConsensusTokenValidator::CheckBlockTokenLimit(const CBlock& block)
{
    return CountTokenOperations(block) <= src20::MAX_TOKENS_PER_BLOCK;
}

std::vector<std::pair<uint256, src20::SRC20Operation>>
ConsensusTokenValidator::GetValidOperations(const CBlock& block, const TokenDB& tokendb, const CCoinsViewCache* view)
{
    std::vector<std::pair<uint256, src20::SRC20Operation>> valid_ops;
    std::set<std::string> block_tickers;

    TokenValidator validator(tokendb);

    for (const auto& tx : block.vtx) {
        auto ops = src20::ParseTransactionSRC20(*tx);

        for (const auto& op : ops) {
            // For issuance, check duplicate tickers within block
            if (op.action == src20::TokenAction::ISSUE) {
                const auto* issuance = op.GetIssuance();
                if (issuance) {
                    if (block_tickers.count(issuance->ticker)) {
                        continue;  // Skip duplicate
                    }
                    block_tickers.insert(issuance->ticker);
                }
            }

            // Derive sender from first input's spent UTXO
            CScript sender;
            if (view && !tx->vin.empty() && !tx->IsCoinBase()) {
                const COutPoint& prevout = tx->vin[0].prevout;
                const Coin& coin = view->AccessCoin(prevout);
                if (!coin.IsSpent()) {
                    sender = coin.out.scriptPubKey;
                }
            }

            auto validation = validator.ValidateOperation(op, *tx, sender);

            if (validation.IsValid()) {
                valid_ops.emplace_back(tx->GetHash().ToUint256(), op);
            } else {
                LogPrintf("Invalid token op in block: %s\n",
                         validation.message.c_str());
            }
        }
    }

    return valid_ops;
}

// Global state management

void InitMempoolTokenState()
{
    g_mempool_tokens = std::make_unique<MempoolTokenState>();
}

void ShutdownMempoolTokenState()
{
    g_mempool_tokens.reset();
}

} // namespace tokens
