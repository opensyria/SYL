// Copyright (c) 2024-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/tokens.h>

#include <key_io.h>
#include <logging.h>
#include <policy/policy.h>
#include <script/script.h>
#include <script/src20.h>
#include <tokens/tokendb.h>
#include <util/overflow.h>
#include <util/translation.h>
#include <wallet/coincontrol.h>
#include <wallet/spend.h>

#include <set>

namespace wallet {

/**
 * SECURITY FIX [L-09]: Format a token balance using integer arithmetic only.
 * Avoids floating-point precision loss that occurs with double division
 * for large balances (e.g., 2^53+ smallest units).
 *
 * @param value Balance in smallest units
 * @param decimals Number of decimal places
 * @return String representation (e.g., "1000.5" for value=10005, decimals=1)
 */
static std::string FormatTokenBalance(uint64_t value, uint8_t decimals)
{
    // AUDIT FIX [ISSUE-014]: Clamp decimals to 18 (protocol max) to prevent
    // uint64_t overflow when computing divisor via repeated multiplication.
    // SRC-20 caps decimals at MAX_DECIMALS (18), but defensive clamping here
    // protects against corrupted DB records or future protocol changes.
    if (decimals > 18) {
        decimals = 18;
    }
    
    if (decimals == 0) {
        return std::to_string(value);
    }

    uint64_t divisor = 1;
    for (int i = 0; i < decimals; i++) divisor *= 10;

    uint64_t whole = value / divisor;
    uint64_t frac = value % divisor;

    std::string frac_str = std::to_string(frac);
    // Left-pad with zeros to match decimals width
    while (static_cast<int>(frac_str.size()) < decimals) {
        frac_str = "0" + frac_str;
    }
    // Trim trailing zeros for cleaner display
    size_t last_nonzero = frac_str.find_last_not_of('0');
    if (last_nonzero != std::string::npos) {
        frac_str = frac_str.substr(0, last_nonzero + 1);
    } else {
        frac_str = "0";
    }

    return std::to_string(whole) + "." + frac_str;
}
bool WalletTokenManager::IsMine(const CScript& script) const
{
    LOCK(m_wallet.cs_wallet);
    return m_wallet.IsMine(script);
}

bool WalletTokenManager::IsMine(const CTxDestination& dest) const
{
    return IsMine(GetScriptForDestination(dest));
}

std::vector<WalletTokenBalance> WalletTokenManager::GetTokenBalances() const
{
    std::vector<WalletTokenBalance> result;
    
    if (!tokens::g_tokendb || !tokens::g_tokendb->IsValid()) {
        return result;
    }

    LOCK(m_wallet.cs_wallet);

    // Get all addresses from wallet
    std::set<CScript> wallet_scripts;
    for (const auto& [dest, label] : m_wallet.m_address_book) {
        wallet_scripts.insert(GetScriptForDestination(dest));
    }
    
    // Also add keypool addresses
    for (const auto& spk_man : m_wallet.GetAllScriptPubKeyMans()) {
        auto scripts = spk_man->GetScriptPubKeys();
        wallet_scripts.insert(scripts.begin(), scripts.end());
    }

    // Aggregate balances across all wallet addresses
    std::map<src20::TokenId, WalletTokenBalance> balances_map;

    for (const auto& script : wallet_scripts) {
        auto addr_balances = tokens::g_tokendb->GetAddressBalances(script);
        
        for (const auto& tb : addr_balances) {
            auto& balance = balances_map[tb.token_id];
            balance.token_id = tb.token_id;
            // DEFENSE-IN-DEPTH: Use CheckedAdd to prevent uint64_t overflow
            // when aggregating balances across many wallet addresses.
            // In practice this cannot overflow (wallet balance ≤ total_supply ≤ UINT64_MAX),
            // but guard defensively.
            auto sum = CheckedAdd(balance.balance, tb.balance);
            balance.balance = sum.value_or(std::numeric_limits<uint64_t>::max());
            
            // Get token info if not already fetched
            if (balance.ticker.empty()) {
                auto info = tokens::g_tokendb->GetTokenInfo(tb.token_id);
                if (info) {
                    balance.ticker = info->ticker;
                    balance.name = info->name;
                    balance.decimals = info->decimals;
                }
            }
        }
    }

    // Convert map to vector and calculate formatted balances
    for (auto& [id, balance] : balances_map) {
        if (balance.balance > 0) {
            // SECURITY FIX [L-09]: Use integer arithmetic for formatting
            // to avoid floating-point precision loss with large token balances.
            balance.balance_formatted = FormatTokenBalance(balance.balance, balance.decimals);
            result.push_back(balance);
        }
    }

    // Sort by ticker
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.ticker < b.ticker;
    });

    return result;
}

std::optional<WalletTokenBalance> WalletTokenManager::GetTokenBalance(
    const src20::TokenId& token_id) const
{
    if (!tokens::g_tokendb || !tokens::g_tokendb->IsValid()) {
        return std::nullopt;
    }

    auto info = tokens::g_tokendb->GetTokenInfo(token_id);
    if (!info) {
        return std::nullopt;
    }

    LOCK(m_wallet.cs_wallet);

    WalletTokenBalance result;
    result.token_id = token_id;
    result.ticker = info->ticker;
    result.name = info->name;
    result.decimals = info->decimals;
    result.balance = 0;

    // AUDIT FIX [M-R5]: Collect all wallet scripts into a set first to prevent
    // double-counting when the same address appears in both m_address_book and
    // GetScriptPubKeys(). Previously balance was summed in two independent loops
    // over overlapping address sources. GetTokenBalances() (plural) already used
    // a set — this singular version now matches that pattern.
    std::set<CScript> wallet_scripts;
    for (const auto& [dest, label] : m_wallet.m_address_book) {
        wallet_scripts.insert(GetScriptForDestination(dest));
    }
    for (const auto& spk_man : m_wallet.GetAllScriptPubKeyMans()) {
        for (const auto& script : spk_man->GetScriptPubKeys()) {
            wallet_scripts.insert(script);
        }
    }

    for (const auto& script : wallet_scripts) {
        // DEFENSE-IN-DEPTH: Use CheckedAdd to prevent uint64_t overflow
        auto partial = tokens::g_tokendb->GetBalance(script, token_id);
        auto sum = CheckedAdd(result.balance, partial);
        result.balance = sum.value_or(std::numeric_limits<uint64_t>::max());
    }

    // SECURITY FIX [L-09]: Use integer arithmetic for formatting
    result.balance_formatted = FormatTokenBalance(result.balance, result.decimals);

    return result;
}

std::vector<WalletTokenTx> WalletTokenManager::GetTokenHistory(
    const std::optional<src20::TokenId>& token_id,
    size_t count) const
{
    std::vector<WalletTokenTx> result;
    
    if (!tokens::g_tokendb || !tokens::g_tokendb->IsValid()) {
        return result;
    }

    LOCK(m_wallet.cs_wallet);

    // Collect all wallet scripts
    std::set<CScript> wallet_scripts;
    for (const auto& [dest, label] : m_wallet.m_address_book) {
        wallet_scripts.insert(GetScriptForDestination(dest));
    }
    for (const auto& spk_man : m_wallet.GetAllScriptPubKeyMans()) {
        auto scripts = spk_man->GetScriptPubKeys();
        wallet_scripts.insert(scripts.begin(), scripts.end());
    }

    // Get history for each wallet address
    for (const auto& script : wallet_scripts) {
        auto history = tokens::g_tokendb->GetAddressHistory(script, token_id, 0, count);
        
        for (const auto& record : history) {
            WalletTokenTx tx;
            tx.token_id = record.token_id;
            tx.txid = record.txid;
            tx.amount = record.amount;
            tx.height = record.height;
            tx.time = record.time;

            // Get token info
            auto info = tokens::g_tokendb->GetTokenInfo(record.token_id);
            if (info) {
                tx.ticker = info->ticker;
                tx.amount_formatted = FormatTokenBalance(tx.amount, info->decimals);
            }

            // Determine from/to and direction
            CTxDestination from_dest, to_dest;
            if (ExtractDestination(record.from_address, from_dest)) {
                tx.from = from_dest;
            }
            if (ExtractDestination(record.to_address, to_dest)) {
                tx.to = to_dest;
            }

            // Is this incoming or outgoing?
            tx.is_incoming = (record.to_address == script);

            // Calculate confirmations from chain tip
            int tip_height = m_wallet.GetLastBlockHeight();
            if (record.height > 0 && tip_height >= record.height) {
                tx.confirmations = tip_height - record.height + 1;
            } else {
                tx.confirmations = 0;  // Unconfirmed or invalid
            }

            result.push_back(tx);
        }
    }

    // AUDIT FIX [R20-03]: Deduplicate intra-wallet transfers. When both
    // from_address and to_address belong to this wallet, GetAddressHistory
    // returns the same TransferRecord for both addresses, causing duplicates.
    // Keep the incoming copy (which is more useful for display) and drop the
    // outgoing duplicate, identified by matching (txid, token_id).
    {
        std::set<std::pair<uint256, src20::TokenId>> seen;
        std::vector<WalletTokenTx> deduped;
        deduped.reserve(result.size());
        for (auto& tx : result) {
            auto key = std::make_pair(tx.txid, tx.token_id);
            if (seen.insert(key).second) {
                deduped.push_back(std::move(tx));
            }
        }
        result = std::move(deduped);
    }

    // Sort by time descending
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.time > b.time;
    });

    // Limit to count
    if (result.size() > count) {
        result.resize(count);
    }

    return result;
}

util::Result<CreatedTransactionResult> WalletTokenManager::CreateIssuanceTransaction(
    const src20::TokenIssuance& issuance,
    const CTxDestination& issuer_dest)
{
    if (!issuance.IsValid()) {
        return util::Error{Untranslated("Invalid issuance parameters")};
    }

    LOCK(m_wallet.cs_wallet);

    // Build the OP_RETURN script for token issuance
    CScript op_return_script = src20::BuildIssuanceScript(issuance);
    
    // Create outputs using CNoDestination for OP_RETURN
    // IMPORTANT: Output ordering matters for token processing!
    // ProcessBlock identifies the issuer from the first non-OP_RETURN output.
    std::vector<CRecipient> recipients;
    
    // Output 0: OP_RETURN with issuance data (use CNoDestination wrapper)
    recipients.push_back({CNoDestination{op_return_script}, 0, false});
    
    // Output 1: Issuer address (dust amount, tokens are tracked in TokenDB)
    // This MUST be the first non-OP_RETURN output!
    recipients.push_back({issuer_dest, DUST_RELAY_TX_FEE, false});

    CCoinControl coin_control;
    
    // Token issuance requires a minimum fee of 100 SYL (enforced by mempool
    // and block validation). Raise the max-fee safety limit accordingly.
    // AUDIT FIX [R18-01]: Uses shared constant from src20.h (was local duplicate).
    coin_control.m_max_tx_fee = src20::MIN_TOKEN_ISSUANCE_FEE * 3; // Allow up to 300 SYL

    // Specify change position to be AFTER the issuer output (position 2)
    // This ensures the issuer address is at output 1 (first non-OP_RETURN)
    auto tx_result = CreateTransaction(m_wallet, recipients, /*change_pos=*/2, coin_control, /*sign=*/true);
    if (!tx_result) return tx_result;

    // If the normal feerate-based fee is below the minimum issuance fee,
    // recompute with a higher feerate derived from the actual tx virtual size.
    if (tx_result->fee < src20::MIN_TOKEN_ISSUANCE_FEE) {
        int64_t tx_vsize = GetVirtualTransactionSize(*tx_result->tx);
        // AUDIT FIX [R15-04]: Use a 1 SYL buffer instead of 10000 sat.
        // Previously 10000 sat was too small: if the second CreateTransaction call
        // selected different UTXOs yielding a slightly smaller vsize, the fee could
        // drop below the minimum (e.g., 99.695 SYL), causing rejection.
        // 1 SYL (100,000,000 sat) provides ample margin.
        coin_control.m_feerate = CFeeRate(src20::MIN_TOKEN_ISSUANCE_FEE + 1 * COIN, (int32_t)tx_vsize);
        coin_control.fOverrideFeeRate = true;
        auto retry_result = CreateTransaction(m_wallet, recipients, /*change_pos=*/2, coin_control, /*sign=*/true);
        // AUDIT FIX [R15-04]: Verify the retry actually meets the minimum fee.
        if (retry_result && retry_result->fee < src20::MIN_TOKEN_ISSUANCE_FEE) {
            return util::Error{Untranslated("Could not construct transaction meeting minimum issuance fee")};
        }
        return retry_result;
    }

    return tx_result;
}

util::Result<CreatedTransactionResult> WalletTokenManager::CreateTransferTransaction(
    const src20::TokenId& token_id,
    const CTxDestination& recipient,
    uint64_t amount,
    const CTxDestination& token_change_dest)
{
    if (!tokens::g_tokendb || !tokens::g_tokendb->IsValid()) {
        return util::Error{Untranslated("Token database not available")};
    }

    // AUDIT FIX [R15-01]: Hold cs_wallet across both the balance check and
    // UTXO selection to prevent a TOCTOU race.  Previously the lock was released
    // between GetTokenBalance() and the coin-selection loop, so a block arriving
    // in the gap could change balances, causing the tx to be built against stale
    // state (wasting the SYL fee when ProcessBlock rejects the transfer).
    LOCK(m_wallet.cs_wallet);

    // AUDIT FIX [R20-02]: Reject zero-amount transfer early.  Without this,
    // an amount=0 call passes the balance check, constructs a valid-looking
    // OP_RETURN tx, broadcasts it, yet gets silently rejected by
    // TransferTokens (amount==0 check), wasting the user's SYL mining fee.
    if (amount == 0) {
        return util::Error{Untranslated("Transfer amount must be greater than zero")};
    }

    // Check balance (under lock)
    auto balance = GetTokenBalance(token_id);
    if (!balance || balance->balance < amount) {
        return util::Error{Untranslated("Insufficient token balance")};
    }

    // CRITICAL: For a valid token transfer, we must spend a UTXO from an address
    // that actually holds the tokens. The TokenDB determines the sender from the
    // scriptPubKey of the first input's spent UTXO.
    
    // Find all addresses in the wallet that hold this token
    std::set<CScript> token_holder_scripts;
    
    for (const auto& [dest, label] : m_wallet.m_address_book) {
        CScript script = GetScriptForDestination(dest);
        uint64_t addr_balance = tokens::g_tokendb->GetBalance(script, token_id);
        if (addr_balance > 0) {
            token_holder_scripts.insert(script);
        }
    }
    
    for (const auto& spk_man : m_wallet.GetAllScriptPubKeyMans()) {
        for (const auto& script : spk_man->GetScriptPubKeys()) {
            uint64_t addr_balance = tokens::g_tokendb->GetBalance(script, token_id);
            if (addr_balance > 0) {
                token_holder_scripts.insert(script);
            }
        }
    }

    if (token_holder_scripts.empty()) {
        return util::Error{Untranslated("No wallet addresses hold this token")};
    }

    // Get available coins and find one from a token holder address
    // SECURITY FIX [R3-01]: Select a UTXO from an address whose per-address
    // token balance is sufficient for the transfer amount.  Previously the
    // code picked the first UTXO from ANY token-holding address, even if that
    // address held fewer tokens than the requested amount.  ProcessBlock
    // derives the sender from vin[0]'s spent output; if that address has
    // insufficient balance the transfer is silently rejected at consensus,
    // burning the user's SYL fee with no token movement.
    CCoinControl coin_control;
    auto coins = AvailableCoins(m_wallet);
    
    bool found_token_coin = false;
    CScript sender_script;  // Track the sender's script for marker output

    for (const auto& coin : coins.All()) {
        if (token_holder_scripts.count(coin.txout.scriptPubKey) > 0) {
            uint64_t addr_balance = tokens::g_tokendb->GetBalance(coin.txout.scriptPubKey, token_id);
            if (addr_balance >= amount) {
                // Pre-select this coin - it must be the first input so TokenDB
                // correctly identifies the sender
                coin_control.Select(coin.outpoint);
                sender_script = coin.txout.scriptPubKey;  // Remember sender address
                found_token_coin = true;
                break;
            }
        }
    }

    if (!found_token_coin) {
        return util::Error{Untranslated("No single wallet address holds enough tokens for this transfer. "
                                         "Tokens are split across multiple addresses.")};
    }

    // Allow additional coins if needed for fees, but the selected coin
    // will be first (important for sender identification)
    coin_control.m_allow_other_inputs = true;

    // Build transfer script
    src20::TokenTransfer transfer(token_id, amount);
    CScript op_return_script = src20::BuildTransferScript(transfer);

    // Create outputs
    // IMPORTANT: Output ordering matters for token processing!
    // GetTransferRecipient expects the recipient at output 1.
    std::vector<CRecipient> recipients;
    
    // Output 0: OP_RETURN with transfer data (use CNoDestination wrapper)
    recipients.push_back({CNoDestination{op_return_script}, 0, false});
    
    // Output 1: Recipient (gets the tokens) - MUST be first non-OP_RETURN!
    recipients.push_back({recipient, DUST_RELAY_TX_FEE, false});
    
    // Output 2: Marker output back to SENDER's address (if partial transfer)
    // This creates a UTXO at the sender's address for future token operations.
    // The sender still owns the remaining tokens (tracked in TokenDB).
    if (balance->balance > amount) {
        // Convert sender script back to destination
        CTxDestination sender_dest;
        if (!ExtractDestination(sender_script, sender_dest)) {
            return util::Error{Untranslated("Could not extract sender destination")};
        }
        recipients.push_back({sender_dest, DUST_RELAY_TX_FEE, false});
    }
    
    // Determine change position: after all our explicit outputs
    // Output 0 = OP_RETURN, Output 1 = Recipient, Output 2 = Sender marker (optional)
    unsigned int change_position = balance->balance > amount ? 3 : 2;
    
    // Create transaction with wallet
    return CreateTransaction(m_wallet, recipients, /*change_pos=*/change_position, coin_control, /*sign=*/true);
}

util::Result<CreatedTransactionResult> WalletTokenManager::CreateBurnTransaction(
    const src20::TokenId& token_id,
    uint64_t amount)
{
    if (!tokens::g_tokendb || !tokens::g_tokendb->IsValid()) {
        return util::Error{Untranslated("Token database not available")};
    }

    // AUDIT FIX [R15-01]: Hold cs_wallet across balance check + coin selection
    // to eliminate the TOCTOU race (same fix as CreateTransferTransaction).
    LOCK(m_wallet.cs_wallet);

    // AUDIT FIX [R20-02]: Reject zero-amount burn early (same rationale as transfer).
    if (amount == 0) {
        return util::Error{Untranslated("Burn amount must be greater than zero")};
    }

    // Check balance (under lock)
    auto balance = GetTokenBalance(token_id);
    if (!balance || balance->balance < amount) {
        return util::Error{Untranslated("Insufficient token balance for burn")};
    }

    // CRITICAL: For a valid token burn, we must spend a UTXO from an address
    // that actually holds the tokens. The TokenDB determines the burner from the
    // scriptPubKey of the first input's spent UTXO.
    
    // Find all addresses in the wallet that hold this token
    std::set<CScript> token_holder_scripts;
    for (const auto& [dest, label] : m_wallet.m_address_book) {
        CScript script = GetScriptForDestination(dest);
        uint64_t addr_balance = tokens::g_tokendb->GetBalance(script, token_id);
        if (addr_balance > 0) {
            token_holder_scripts.insert(script);
        }
    }
    for (const auto& spk_man : m_wallet.GetAllScriptPubKeyMans()) {
        for (const auto& script : spk_man->GetScriptPubKeys()) {
            uint64_t addr_balance = tokens::g_tokendb->GetBalance(script, token_id);
            if (addr_balance > 0) {
                token_holder_scripts.insert(script);
            }
        }
    }

    if (token_holder_scripts.empty()) {
        return util::Error{Untranslated("No wallet addresses hold this token")};
    }

    // Get available coins and find one from a token holder address
    // SECURITY FIX [R3-01]: Same fix as CreateTransferTransaction — ensure
    // the selected UTXO's address has sufficient per-address balance for the
    // burn amount, not just sufficient aggregate wallet balance.
    CCoinControl coin_control;
    auto coins = AvailableCoins(m_wallet);
    bool found_token_coin = false;
    CScript sender_script;

    for (const auto& coin : coins.All()) {
        if (token_holder_scripts.count(coin.txout.scriptPubKey) > 0) {
            uint64_t addr_balance = tokens::g_tokendb->GetBalance(coin.txout.scriptPubKey, token_id);
            if (addr_balance >= amount) {
                // Pre-select this coin - it must be the first input so TokenDB
                // correctly identifies the burner
                coin_control.Select(coin.outpoint);
                sender_script = coin.txout.scriptPubKey;
                found_token_coin = true;
                break;
            }
        }
    }

    if (!found_token_coin) {
        return util::Error{Untranslated("No single wallet address holds enough tokens for this burn. "
                                         "Tokens are split across multiple addresses.")};
    }

    // Allow additional coins if needed for fees
    coin_control.m_allow_other_inputs = true;

    // Build burn script
    src20::TokenBurn burn(token_id, amount);
    CScript op_return_script = src20::BuildBurnScript(burn);

    // Create outputs
    std::vector<CRecipient> recipients;
    
    // Output 0: OP_RETURN with burn data (use CNoDestination wrapper)
    recipients.push_back({CNoDestination{op_return_script}, 0, false});

    // Output 1: Marker output back to burner's address (if tokens remain).
    // This creates a UTXO at the holder address for future token operations.
    unsigned int change_pos = 1;
    if (balance->balance > amount) {
        CTxDestination sender_dest;
        if (!ExtractDestination(sender_script, sender_dest)) {
            return util::Error{Untranslated("Could not extract sender destination")};
        }
        recipients.push_back({sender_dest, DUST_RELAY_TX_FEE, false});
        change_pos = 2;
    }
    
    return CreateTransaction(m_wallet, recipients, change_pos, coin_control, /*sign=*/true);
}

CAmount WalletTokenManager::EstimateTokenTxFee(src20::TokenAction action) const
{
    // Estimate transaction size based on action
    size_t tx_size = 0;
    
    switch (action) {
        case src20::TokenAction::ISSUE:
            // 1 input + OP_RETURN + issuer output + change
            tx_size = 180 + 80 + 34 + 34;  // ~328 vbytes
            break;
        case src20::TokenAction::TRANSFER:
            // 1 input + OP_RETURN + recipient + optional change
            tx_size = 180 + 50 + 34 + 34;  // ~298 vbytes
            break;
        case src20::TokenAction::BURN:
            // 1 input + OP_RETURN + change
            tx_size = 180 + 50 + 34;  // ~264 vbytes
            break;
        default:
            tx_size = 300;
            break;
    }

    // Use minimum fee rate
    CFeeRate fee_rate = m_wallet.chain().relayMinFee();
    return fee_rate.GetFee(tx_size);
}

bool WalletTokenManager::HasSufficientTokenBalance(
    const src20::TokenId& token_id,
    uint64_t amount) const
{
    auto balance = GetTokenBalance(token_id);
    return balance && balance->balance >= amount;
}

std::vector<CTxDestination> WalletTokenManager::GetTokenAddresses(
    const src20::TokenId& token_id) const
{
    std::vector<CTxDestination> result;
    
    if (!tokens::g_tokendb || !tokens::g_tokendb->IsValid()) {
        return result;
    }

    LOCK(m_wallet.cs_wallet);

    // AUDIT FIX [R15-03]: Also iterate ScriptPubKeyMans to catch keypool/change
    // addresses not yet in m_address_book (matching GetTokenBalance/GetTokenBalances).
    std::set<CScript> wallet_scripts;
    for (const auto& [dest, label] : m_wallet.m_address_book) {
        wallet_scripts.insert(GetScriptForDestination(dest));
    }
    for (const auto& spk_man : m_wallet.GetAllScriptPubKeyMans()) {
        for (const auto& script : spk_man->GetScriptPubKeys()) {
            wallet_scripts.insert(script);
        }
    }

    for (const auto& script : wallet_scripts) {
        uint64_t bal = tokens::g_tokendb->GetBalance(script, token_id);
        if (bal > 0) {
            CTxDestination dest;
            if (ExtractDestination(script, dest)) {
                result.push_back(dest);
            }
        }
    }

    return result;
}

// Token RPC helpers

namespace token_rpc {

std::vector<CTxOut> BuildIssuanceOutputs(
    const src20::TokenIssuance& issuance,
    const CScript& issuer_script,
    CAmount dust_amount)
{
    std::vector<CTxOut> outputs;
    
    // OP_RETURN with issuance data
    CScript op_return = src20::BuildIssuanceScript(issuance);
    outputs.emplace_back(0, op_return);
    
    // Issuer address
    outputs.emplace_back(dust_amount, issuer_script);
    
    return outputs;
}

std::vector<CTxOut> BuildTransferOutputs(
    const src20::TokenTransfer& transfer,
    const CScript& recipient_script,
    const std::optional<CScript>& change_script,
    CAmount dust_amount)
{
    std::vector<CTxOut> outputs;
    
    // OP_RETURN with transfer data
    CScript op_return = src20::BuildTransferScript(transfer);
    outputs.emplace_back(0, op_return);
    
    // Recipient
    outputs.emplace_back(dust_amount, recipient_script);
    
    // Optional token change
    if (change_script) {
        outputs.emplace_back(dust_amount, *change_script);
    }
    
    return outputs;
}

std::vector<CTxOut> BuildBurnOutputs(
    const src20::TokenBurn& burn,
    CAmount dust_amount)
{
    std::vector<CTxOut> outputs;
    
    // OP_RETURN with burn data
    CScript op_return = src20::BuildBurnScript(burn);
    outputs.emplace_back(0, op_return);
    
    return outputs;
}

} // namespace token_rpc

} // namespace wallet
