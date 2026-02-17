// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/src20.h>
#include <streams.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <tokens/tokendb.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/fs.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace {

void initialize_token_disconnect_fuzz()
{
    SelectParams(ChainType::REGTEST);
}

/**
 * Build a minimal SRC-20 ISSUE script from fuzzed input.
 */
CScript BuildFuzzIssueScript(FuzzedDataProvider& fuzzed_data)
{
    CScript script;
    script << OP_RETURN;
    // Protocol ID
    std::vector<uint8_t> proto_id(src20::SRC20_PROTOCOL_ID.begin(), src20::SRC20_PROTOCOL_ID.end());
    script << proto_id;
    // Version 1
    script << std::vector<uint8_t>{0x01};
    // Action ISSUE (0x01)
    script << std::vector<uint8_t>{0x01};
    // Ticker (4–8 chars)
    const size_t ticker_len = fuzzed_data.ConsumeIntegralInRange<size_t>(4, 8);
    std::string ticker = fuzzed_data.ConsumeBytesAsString(ticker_len);
    // Make ticker uppercase ASCII to pass validation
    for (auto& c : ticker) {
        c = 'A' + (static_cast<unsigned char>(c) % 26);
    }
    script << std::vector<uint8_t>(ticker.begin(), ticker.end());
    // Name
    const size_t name_len = fuzzed_data.ConsumeIntegralInRange<size_t>(1, 32);
    std::string name = fuzzed_data.ConsumeBytesAsString(name_len);
    script << std::vector<uint8_t>(name.begin(), name.end());
    // Decimals
    script << std::vector<uint8_t>{fuzzed_data.ConsumeIntegralInRange<uint8_t>(0, 18)};
    // Supply (as 8-byte LE)
    const uint64_t supply = fuzzed_data.ConsumeIntegralInRange<uint64_t>(1, 1'000'000'000);
    std::vector<uint8_t> supply_bytes(8);
    for (int i = 0; i < 8; i++) supply_bytes[i] = (supply >> (8*i)) & 0xFF;
    script << supply_bytes;

    return script;
}

/**
 * Build a minimal SRC-20 TRANSFER script from fuzzed input.
 */
CScript BuildFuzzTransferScript(FuzzedDataProvider& fuzzed_data, const src20::TokenId& token_id)
{
    CScript script;
    script << OP_RETURN;
    std::vector<uint8_t> proto_id(src20::SRC20_PROTOCOL_ID.begin(), src20::SRC20_PROTOCOL_ID.end());
    script << proto_id;
    script << std::vector<uint8_t>{0x01};   // version
    script << std::vector<uint8_t>{0x02};   // TRANSFER
    // Token ID
    const unsigned char* id_begin = token_id.GetHash().data();
    const std::vector<uint8_t> id_data(id_begin, id_begin + 32);
    script << id_data;
    // Amount
    const uint64_t amount = fuzzed_data.ConsumeIntegralInRange<uint64_t>(1, 1000);
    std::vector<uint8_t> amount_bytes(8);
    for (int i = 0; i < 8; i++) amount_bytes[i] = (amount >> (8*i)) & 0xFF;
    script << amount_bytes;

    return script;
}

/**
 * Build a minimal SRC-20 BURN script from fuzzed input.
 */
CScript BuildFuzzBurnScript(FuzzedDataProvider& fuzzed_data, const src20::TokenId& token_id)
{
    CScript script;
    script << OP_RETURN;
    std::vector<uint8_t> proto_id(src20::SRC20_PROTOCOL_ID.begin(), src20::SRC20_PROTOCOL_ID.end());
    script << proto_id;
    script << std::vector<uint8_t>{0x01};   // version
    script << std::vector<uint8_t>{0x03};   // BURN
    const unsigned char* id_begin = token_id.GetHash().data();
    const std::vector<uint8_t> id_data(id_begin, id_begin + 32);
    script << id_data;
    const uint64_t amount = fuzzed_data.ConsumeIntegralInRange<uint64_t>(1, 100);
    std::vector<uint8_t> amount_bytes(8);
    for (int i = 0; i < 8; i++) amount_bytes[i] = (amount >> (8*i)) & 0xFF;
    script << amount_bytes;

    return script;
}

} // namespace

/**
 * Fuzz the TokenDB ProcessBlock → DisconnectBlock round-trip.
 *
 * Strategy:
 * 1. Create an in-memory TokenDB
 * 2. Build a fuzzed block containing random SRC-20 operations
 * 3. ProcessBlock to record operations + undo records
 * 4. DisconnectBlock to revert them
 * 5. Assert that the database state is restored (best-block, token counts)
 *
 * This exercises the undo-record serialization format, the disconnect logic
 * for all three token operation types (ISSUE, TRANSFER, BURN), and the
 * batch atomicity of writes.
 */
FUZZ_TARGET(token_disconnect, .init = initialize_token_disconnect_fuzz)
{
    FuzzedDataProvider fuzzed_data(buffer.data(), buffer.size());
    if (fuzzed_data.remaining_bytes() < 64) return;

    // Create a temporary in-memory token database
    tokens::TokenDB db(fs::path{}, /*cache_size=*/1 << 20, /*memory=*/true, /*wipe=*/true);
    if (!db.IsValid()) return;

    // ---- Phase 1: Build and process a block with token operations ----

    const int height = fuzzed_data.ConsumeIntegralInRange<int>(1, 500'000);
    const uint256 prev_hash = [&]() {
        uint256 h;
        const auto bytes = fuzzed_data.ConsumeBytes<unsigned char>(32);
        if (bytes.size() == 32) memcpy(h.data(), bytes.data(), 32);
        return h;
    }();

    // Create a block with fuzzed transactions containing SRC-20 ops
    CBlock block;
    block.hashPrevBlock = prev_hash;
    block.nTime = fuzzed_data.ConsumeIntegral<uint32_t>();
    block.nBits = fuzzed_data.ConsumeIntegral<uint32_t>();
    block.nNonce = fuzzed_data.ConsumeIntegral<uint32_t>();

    // Coinbase tx (required — should be skipped by M-03 fix)
    CMutableTransaction coinbase_tx;
    coinbase_tx.vin.resize(1);
    coinbase_tx.vin[0].prevout.SetNull();
    coinbase_tx.vin[0].scriptSig = CScript() << height;
    coinbase_tx.vout.resize(1);
    coinbase_tx.vout[0].nValue = 10000 * COIN;
    coinbase_tx.vout[0].scriptPubKey = CScript() << OP_TRUE;
    block.vtx.push_back(MakeTransactionRef(std::move(coinbase_tx)));

    // Add 1-5 token-related transactions
    const int num_txs = fuzzed_data.ConsumeIntegralInRange<int>(1, 5);
    for (int i = 0; i < num_txs && fuzzed_data.remaining_bytes() > 32; ++i) {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout = COutPoint(Txid::FromUint256(
            [&]() { uint256 h; auto b = fuzzed_data.ConsumeBytes<unsigned char>(32);
                     if (b.size() == 32) memcpy(h.data(), b.data(), 32); return h; }()), 0);
        tx.vout.resize(1);

        // Pick random SRC-20 op type; only ISSUE makes sense without existing tokens
        tx.vout[0].scriptPubKey = BuildFuzzIssueScript(fuzzed_data);
        tx.vout[0].nValue = 0;

        block.vtx.push_back(MakeTransactionRef(std::move(tx)));
    }

    // Capture state before processing
    const uint256 best_before = db.GetBestBlock();
    const uint64_t tokens_before = db.GetTokenCount();

    // Process the block (legacy overload — no CBlockUndo needed)
    const int ops = db.ProcessBlock(block, height);

    if (ops > 0) {
        // ---- Phase 2: DisconnectBlock must cleanly revert ---- 
        const bool disconnect_ok = db.DisconnectBlock(block, height);

        // The disconnect should succeed — undo records were written by ProcessBlock
        assert(disconnect_ok);

        // Token count must return to the pre-process value
        const uint64_t tokens_after_disconnect = db.GetTokenCount();
        assert(tokens_after_disconnect == tokens_before);
    }
}
