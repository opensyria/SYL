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
#include <util/translation.h>
#include <wallet/coincontrol.h>
#include <wallet/spend.h>

#include <set>

namespace wallet {

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
            balance.balance += tb.balance;
            
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
            double divisor = std::pow(10, balance.decimals);
            balance.balance_formatted = balance.balance / divisor;
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
        result.balance += tokens::g_tokendb->GetBalance(script, token_id);
    }

    double divisor = std::pow(10, result.decimals);
    result.balance_formatted = result.balance / divisor;

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
                double divisor = std::pow(10, info->decimals);
                tx.amount_formatted = tx.amount / divisor;
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
    
    // Specify change position to be AFTER the issuer output (position 2)
    // This ensures the issuer address is at output 1 (first non-OP_RETURN)
    return CreateTransaction(m_wallet, recipients, /*change_pos=*/2, coin_control, /*sign=*/true);
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

    // Check balance
    auto balance = GetTokenBalance(token_id);
    if (!balance || balance->balance < amount) {
        return util::Error{Untranslated("Insufficient token balance")};
    }

    LOCK(m_wallet.cs_wallet);
    
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
    CCoinControl coin_control;
    auto coins = AvailableCoins(m_wallet);
    
    bool found_token_coin = false;
    CScript sender_script;  // Track the sender's script for marker output

    for (const auto& coin : coins.All()) {
        if (token_holder_scripts.count(coin.txout.scriptPubKey) > 0) {
            // Pre-select this coin - it must be the first input so TokenDB
            // correctly identifies the sender
            coin_control.Select(coin.outpoint);
            sender_script = coin.txout.scriptPubKey;  // Remember sender address
            found_token_coin = true;
            break;
        }
    }

    if (!found_token_coin) {
        return util::Error{Untranslated("No spendable UTXO found for token holder address")};
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

    // Check balance
    auto balance = GetTokenBalance(token_id);
    if (!balance || balance->balance < amount) {
        return util::Error{Untranslated("Insufficient token balance for burn")};
    }

    LOCK(m_wallet.cs_wallet);

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
    CCoinControl coin_control;
    auto coins = AvailableCoins(m_wallet);
    bool found_token_coin = false;

    for (const auto& coin : coins.All()) {
        if (token_holder_scripts.count(coin.txout.scriptPubKey) > 0) {
            // Pre-select this coin - it must be the first input so TokenDB
            // correctly identifies the burner
            coin_control.Select(coin.outpoint);
            found_token_coin = true;
            break;
        }
    }

    if (!found_token_coin) {
        return util::Error{Untranslated("No spendable UTXO found for token holder address")};
    }

    // Allow additional coins if needed for fees
    coin_control.m_allow_other_inputs = true;

    // Build burn script
    src20::TokenBurn burn(token_id, amount);
    CScript op_return_script = src20::BuildBurnScript(burn);

    // Create outputs
    // For burn, the output order is simpler: OP_RETURN + change
    std::vector<CRecipient> recipients;
    
    // Output 0: OP_RETURN with burn data (use CNoDestination wrapper)
    recipients.push_back({CNoDestination{op_return_script}, 0, false});
    
    // Change goes at position 1 (after OP_RETURN)
    return CreateTransaction(m_wallet, recipients, /*change_pos=*/1, coin_control, /*sign=*/true);
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

    for (const auto& [dest, label] : m_wallet.m_address_book) {
        CScript script = GetScriptForDestination(dest);
        uint64_t balance = tokens::g_tokendb->GetBalance(script, token_id);
        if (balance > 0) {
            result.push_back(dest);
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
