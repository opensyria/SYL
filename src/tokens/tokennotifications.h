// Copyright (c) 2024-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENSY_TOKENS_TOKENNOTIFICATIONS_H
#define OPENSY_TOKENS_TOKENNOTIFICATIONS_H

#include <kernel/mempool_removal_reason.h>
#include <validationinterface.h>

#include <memory>

namespace tokens {

/**
 * Token Validation Interface
 * 
 * Subscribes to blockchain validation events to process token operations
 * in real-time as blocks are connected and disconnected.
 * 
 * This ensures that:
 * - Token issuances, transfers, and burns are processed when blocks are connected
 * - Token state is correctly rolled back during chain reorganizations
 * - Mempool token state is cleaned up appropriately
 * 
 * Thread Safety:
 * - All callbacks run on the validation background thread
 * - Internal state is protected by appropriate locks in TokenDB and MempoolTokenState
 */
class TokenValidationInterface final : public CValidationInterface
{
public:
    TokenValidationInterface() = default;
    ~TokenValidationInterface() = default;

    // Non-copyable
    TokenValidationInterface(const TokenValidationInterface&) = delete;
    TokenValidationInterface& operator=(const TokenValidationInterface&) = delete;

protected:
    /**
     * Called when a block is connected to the active chain.
     * 
     * Processes all SRC-20 token operations in the block:
     * - Token issuances (ISSUE)
     * - Token transfers (TRANSFER)  
     * - Token burns (BURN)
     * 
     * Creates undo records for chain reorganization support.
     */
    void BlockConnected(ChainstateRole role,
                       const std::shared_ptr<const CBlock>& block,
                       const CBlockIndex* pindex) override;

    /**
     * Called when a block is disconnected from the active chain (reorg).
     * 
     * Reverts all SRC-20 token operations that were in the disconnected block
     * using the stored undo records.
     */
    void BlockDisconnected(const std::shared_ptr<const CBlock>& block,
                          const CBlockIndex* pindex) override;

    /**
     * Called when the chain state is flushed to disk.
     * 
     * Syncs the token database to ensure durability.
     */
    void ChainStateFlushed(ChainstateRole role,
                          const CBlockLocator& locator) override;

    /**
     * Called when transactions are removed from mempool for a block.
     * 
     * Cleans up pending token state for transactions that were mined.
     */
    void MempoolTransactionsRemovedForBlock(
        const std::vector<RemovedMempoolTransactionInfo>& txs_removed_for_block,
        unsigned int nBlockHeight) override;

    /**
     * Called when a transaction is added to the mempool.
     * 
     * Tracks pending token operations for DoS protection:
     * - Prevents duplicate ticker issuances in mempool
     * - Tracks pending balance changes
     * - Enforces per-address operation limits
     */
    void TransactionAddedToMempool(const NewMempoolTransactionInfo& tx_info,
                                  uint64_t mempool_sequence) override;

    /**
     * Called when a transaction is removed from mempool for any reason
     * (eviction, expiry, RBF replacement, conflict, reorg).
     *
     * Cleans up pending token state to prevent stale ticker reservations
     * and incorrect pending balance tracking.
     */
    void TransactionRemovedFromMempool(const CTransactionRef& tx,
                                       MemPoolRemovalReason reason,
                                       uint64_t mempool_sequence) override;
};

/** Global token validation interface instance */
extern std::unique_ptr<TokenValidationInterface> g_token_validation_interface;

/**
 * Initialize the token validation interface and register it.
 * Must be called after token database is initialized.
 * 
 * @param validation_signals The validation signals to register with
 * @return true if successful
 */
bool InitTokenValidationInterface(ValidationSignals& validation_signals);

/**
 * Shutdown the token validation interface.
 * Must be called before token database is shutdown.
 * 
 * @param validation_signals The validation signals to unregister from
 */
void ShutdownTokenValidationInterface(ValidationSignals& validation_signals);

} // namespace tokens

#endif // OPENSY_TOKENS_TOKENNOTIFICATIONS_H
