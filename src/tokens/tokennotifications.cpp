// Copyright (c) 2024-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <tokens/tokennotifications.h>

#include <coins.h>
#include <kernel/mempool_entry.h>
#include <kernel/mempool_removal_reason.h>
#include <logging.h>
#include <primitives/block.h>
#include <script/src20.h>
#include <tokens/tokendb.h>
#include <tokens/tokenvalidation.h>
#include <txmempool.h>
#include <validation.h>

namespace tokens {

std::unique_ptr<TokenValidationInterface> g_token_validation_interface;

void TokenValidationInterface::BlockConnected(
    ChainstateRole role,
    const std::shared_ptr<const CBlock>& block,
    const CBlockIndex* pindex)
{
    // Only process for the active chainstate
    if (role != ChainstateRole::NORMAL) {
        return;
    }

    if (!g_tokendb || !g_tokendb->IsValid()) {
        return;
    }

    if (!block || !pindex) {
        return;
    }

    // NOTE: Token block processing is now done in Chainstate::ConnectBlock()
    // where we have access to the CBlockUndo data for sender identification.
    // This callback only handles mempool cleanup.

    // Clean up mempool token state for transactions in this block
    if (g_mempool_tokens) {
        for (const auto& tx : block->vtx) {
            g_mempool_tokens->RemoveTransaction(tx->GetHash().ToUint256());
        }
        
        // Periodically prune stale rate limit entries to prevent memory bloat
        // Do this every block to keep memory bounded
        g_mempool_tokens->PruneStaleRateLimitEntries();
    }
}

void TokenValidationInterface::BlockDisconnected(
    const std::shared_ptr<const CBlock>& block,
    const CBlockIndex* pindex)
{
    // AUDIT FIX [M-2]: Token disconnect is now performed atomically in
    // Chainstate::DisconnectTip() BEFORE the UTXO view is flushed.
    // This signal handler previously handled disconnect, but that was
    // non-atomic with the UTXO rollback — a crash between the two would
    // leave token state stranded on the old chain.
    //
    // This callback is retained only for future use or additional cleanup
    // that doesn't need atomicity guarantees.
    (void)block;
    (void)pindex;
}

void TokenValidationInterface::ChainStateFlushed(
    ChainstateRole role,
    const CBlockLocator& locator)
{
    // Only sync for the active chainstate
    if (role != ChainstateRole::NORMAL) {
        return;
    }

    if (!g_tokendb || !g_tokendb->IsValid()) {
        return;
    }

    // Sync token database to disk for durability
    g_tokendb->Sync();
    LogDebug(BCLog::TOKEN, "ChainStateFlushed: synced token database\n");
}

void TokenValidationInterface::MempoolTransactionsRemovedForBlock(
    const std::vector<RemovedMempoolTransactionInfo>& txs_removed_for_block,
    unsigned int nBlockHeight)
{
    if (!g_mempool_tokens) {
        return;
    }

    // Remove these transactions from pending mempool token state
    // (They're now confirmed in a block)
    for (const auto& removed_tx : txs_removed_for_block) {
        g_mempool_tokens->RemoveTransaction(removed_tx.info.m_tx->GetHash().ToUint256());
    }
}

void TokenValidationInterface::TransactionAddedToMempool(
    const NewMempoolTransactionInfo& tx_info,
    uint64_t mempool_sequence)
{
    // NOTE: Token mempool tracking is now handled directly in MemPoolAccept::SubmitPackage
    // and AcceptSingleTransactionInternal in validation.cpp, where we have access to
    // the coins view and can properly identify the sender's scriptPubKey.
    // This callback is kept for potential future use but does not add tokens.
    
    // The correct sender is stored in Workspace::m_token_sender during PreChecks
    // and used when the transaction is finalized into the mempool.
}

void TokenValidationInterface::TransactionRemovedFromMempool(
    const CTransactionRef& tx,
    MemPoolRemovalReason reason,
    uint64_t mempool_sequence)
{
    // Clean up pending token state when a transaction is removed from mempool
    // for ANY reason (eviction, expiry, RBF, conflict, reorg).
    // Note: BLOCK removal is handled by MempoolTransactionsRemovedForBlock,
    // but we handle it here too for safety.
    
    if (!g_mempool_tokens || !tx) {
        return;
    }
    
    // Check if this is a token transaction before doing any work
    auto ops = src20::ParseTransactionSRC20(*tx);
    if (ops.empty()) {
        return;  // Not a token transaction
    }
    
    // Remove from mempool token state (reverses pending balance changes, frees ticker)
    g_mempool_tokens->RemoveTransaction(tx->GetHash().ToUint256());
    
    LogDebug(BCLog::TOKEN, "TransactionRemovedFromMempool: cleaned up token tx %s (reason: %s)\n",
             tx->GetHash().ToString(), RemovalReasonToString(reason));
}

bool InitTokenValidationInterface(ValidationSignals& validation_signals)
{
    if (g_token_validation_interface) {
        LogPrintf("WARNING: Token validation interface already initialized\n");
        return true;
    }

    g_token_validation_interface = std::make_unique<TokenValidationInterface>();
    validation_signals.RegisterValidationInterface(g_token_validation_interface.get());

    LogDebug(BCLog::TOKEN, "Token validation interface initialized and registered\n");
    return true;
}

void ShutdownTokenValidationInterface(ValidationSignals& validation_signals)
{
    if (g_token_validation_interface) {
        validation_signals.UnregisterValidationInterface(g_token_validation_interface.get());
        // Note: FlushBackgroundCallbacks() is already called earlier in Shutdown()
        // before we get here, so all pending callbacks have already been processed.
        g_token_validation_interface.reset();
        LogDebug(BCLog::TOKEN, "Token validation interface shutdown complete\n");
    }
}

} // namespace tokens
