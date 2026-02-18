// Copyright (c) 2024-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENSY_WALLET_TOKENS_H
#define OPENSY_WALLET_TOKENS_H

#include <script/src20.h>
#include <tokens/tokendb.h>
#include <util/result.h>
#include <wallet/spend.h>
#include <wallet/wallet.h>

#include <optional>
#include <string>
#include <vector>

namespace wallet {

/**
 * Token balance for display in wallet
 */
struct WalletTokenBalance {
    src20::TokenId token_id;
    std::string ticker;
    std::string name;
    uint8_t decimals;
    uint64_t balance;           // In smallest units
    std::string balance_formatted;   // Human-readable (string to avoid floating-point precision loss)
    
    WalletTokenBalance() : decimals(0), balance(0), balance_formatted("0") {}
};

/**
 * Token transaction for history display
 */
struct WalletTokenTx {
    src20::TokenId token_id;
    std::string ticker;
    uint256 txid;
    CTxDestination from;
    CTxDestination to;
    uint64_t amount;
    std::string amount_formatted;
    int height;
    int64_t time;
    bool is_incoming;           // true if we received tokens
    int confirmations;

    WalletTokenTx() : amount(0), amount_formatted("0"), height(0), time(0), 
                      is_incoming(false), confirmations(0) {}
};

/**
 * Wallet token manager
 * 
 * Handles token-related functionality for the wallet:
 * - Balance tracking
 * - Transaction building
 * - History display
 */
class WalletTokenManager {
private:
    CWallet& m_wallet;
    
    // Check if an address belongs to this wallet
    bool IsMine(const CScript& script) const;
    bool IsMine(const CTxDestination& dest) const;

public:
    explicit WalletTokenManager(CWallet& wallet) : m_wallet(wallet) {}

    /**
     * Get all token balances for the wallet
     */
    std::vector<WalletTokenBalance> GetTokenBalances() const;

    /**
     * Get balance for a specific token
     */
    std::optional<WalletTokenBalance> GetTokenBalance(const src20::TokenId& token_id) const;

    /**
     * Get token transaction history for the wallet
     */
    std::vector<WalletTokenTx> GetTokenHistory(
        const std::optional<src20::TokenId>& token_id = std::nullopt,
        size_t count = 100) const;

    /**
     * Create a token issuance transaction
     * 
     * @param issuance Token issuance parameters
     * @param issuer_dest Address that will receive the tokens
     * @return CreatedTransactionResult on success, error on failure
     */
    util::Result<CreatedTransactionResult> CreateIssuanceTransaction(
        const src20::TokenIssuance& issuance,
        const CTxDestination& issuer_dest);

    /**
     * Create a token transfer transaction
     * 
     * @param token_id Token to transfer
     * @param recipient Recipient address
     * @param amount Amount to transfer
     * @param token_change_dest Where to send remaining tokens
     * @return CreatedTransactionResult on success, error on failure
     */
    util::Result<CreatedTransactionResult> CreateTransferTransaction(
        const src20::TokenId& token_id,
        const CTxDestination& recipient,
        uint64_t amount,
        const CTxDestination& token_change_dest);

    /**
     * Create a token burn transaction
     * 
     * @param token_id Token to burn
     * @param amount Amount to burn
     * @return CreatedTransactionResult on success, error on failure
     */
    util::Result<CreatedTransactionResult> CreateBurnTransaction(
        const src20::TokenId& token_id,
        uint64_t amount);

    /**
     * Estimate fee for a token transaction
     * 
     * @param action Token action type
     * @return Estimated fee in qirsh (smallest SYL unit)
     */
    CAmount EstimateTokenTxFee(src20::TokenAction action) const;

    /**
     * Check if wallet has sufficient token balance
     */
    bool HasSufficientTokenBalance(const src20::TokenId& token_id, uint64_t amount) const;

    /**
     * Get list of token-holding addresses in this wallet
     */
    std::vector<CTxDestination> GetTokenAddresses(const src20::TokenId& token_id) const;
};

/**
 * Wallet RPC helpers for tokens
 */
namespace token_rpc {

/**
 * Build issuance transaction outputs
 */
std::vector<CTxOut> BuildIssuanceOutputs(
    const src20::TokenIssuance& issuance,
    const CScript& issuer_script,
    CAmount dust_amount);

/**
 * Build transfer transaction outputs
 */
std::vector<CTxOut> BuildTransferOutputs(
    const src20::TokenTransfer& transfer,
    const CScript& recipient_script,
    const std::optional<CScript>& change_script,
    CAmount dust_amount);

/**
 * Build burn transaction outputs
 */
std::vector<CTxOut> BuildBurnOutputs(
    const src20::TokenBurn& burn,
    CAmount dust_amount);

} // namespace token_rpc

} // namespace wallet

#endif // OPENSY_WALLET_TOKENS_H
