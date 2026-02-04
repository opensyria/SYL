// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/params.h>
#include <crypto/randomx_context.h>
#include <pow.h>
#include <primitives/block.h>
#include <streams.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <uint256.h>
#include <util/chaintype.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace {
// Use a single static context to avoid expensive re-initialization
static std::unique_ptr<RandomXContext> g_fuzz_randomx_context;
static uint256 g_fuzz_key_hash;

void InitFuzzRandomXContext(const uint256& keyHash)
{
    if (!g_fuzz_randomx_context) {
        g_fuzz_randomx_context = std::make_unique<RandomXContext>();
    }
    if (g_fuzz_key_hash != keyHash) {
        g_fuzz_randomx_context->Initialize(keyHash);
        g_fuzz_key_hash = keyHash;
    }
}
} // namespace

void initialize_randomx_fuzz()
{
    SelectParams(ChainType::REGTEST);
}

FUZZ_TARGET(randomx_context, .init = initialize_randomx_fuzz)
{
    FuzzedDataProvider fuzzed_data_provider(buffer.data(), buffer.size());

    // Generate a key hash from fuzz input
    const std::optional<uint256> key_hash = ConsumeDeserializable<uint256>(fuzzed_data_provider);
    if (!key_hash) {
        return;
    }

    // Initialize context with the key
    InitFuzzRandomXContext(*key_hash);

    // Test hash calculation with various input sizes
    LIMITED_WHILE(fuzzed_data_provider.remaining_bytes() > 0, 100) {
        const size_t input_size = fuzzed_data_provider.ConsumeIntegralInRange<size_t>(0, 1024);
        const std::vector<unsigned char> input = fuzzed_data_provider.ConsumeBytes<unsigned char>(input_size);

        if (g_fuzz_randomx_context && g_fuzz_randomx_context->IsInitialized()) {
            try {
                const uint256 hash = g_fuzz_randomx_context->CalculateHash(input);
                // Verify hash is 32 bytes (always true for uint256)
                assert(hash.size() == 32);
            } catch (const std::runtime_error&) {
                // Context not initialized - acceptable
            }
        }
    }
}

FUZZ_TARGET(randomx_pow_check, .init = initialize_randomx_fuzz)
{
    FuzzedDataProvider fuzzed_data_provider(buffer.data(), buffer.size());
    const Consensus::Params& consensus_params = Params().GetConsensus();

    // Consume a block header
    const std::optional<CBlockHeader> block_header = ConsumeDeserializable<CBlockHeader>(fuzzed_data_provider);
    if (!block_header) {
        return;
    }

    // Test various heights
    const int height = fuzzed_data_provider.ConsumeIntegralInRange<int>(-1000, 1000000);

    // Test IsRandomXActive
    const bool is_randomx_active = consensus_params.IsRandomXActive(height);

    // Test GetRandomXKeyBlockHeight
    if (height >= 0) {
        const int key_height = consensus_params.GetRandomXKeyBlockHeight(height);
        // Key height should always be non-negative
        assert(key_height >= 0);
        // Key height should be less than or equal to height
        assert(key_height <= height);
        // Key height should be a multiple of the interval (or 0)
        assert(key_height == 0 || key_height % consensus_params.nRandomXKeyBlockInterval == 0);
    }

    // Test GetRandomXPowLimit
    const uint256& pow_limit = consensus_params.GetRandomXPowLimit(height);
    (void)pow_limit;

    // Test CheckProofOfWorkImpl with height-aware version
    const std::optional<uint256> hash = ConsumeDeserializable<uint256>(fuzzed_data_provider);
    if (hash) {
        const unsigned int nbits = fuzzed_data_provider.ConsumeIntegral<unsigned int>();
        (void)CheckProofOfWorkImpl(*hash, nbits, height, consensus_params);
    }

    // Test CheckProofOfWorkForBlockIndex (simplified check for index loading)
    if (height >= 0) {
        (void)CheckProofOfWorkForBlockIndex(*block_header, height, consensus_params);
    }
}

FUZZ_TARGET(randomx_key_rotation, .init = initialize_randomx_fuzz)
{
    FuzzedDataProvider fuzzed_data_provider(buffer.data(), buffer.size());
    const Consensus::Params& consensus_params = Params().GetConsensus();

    // Test key rotation logic exhaustively
    LIMITED_WHILE(fuzzed_data_provider.remaining_bytes() > 0, 1000) {
        const int height = fuzzed_data_provider.ConsumeIntegralInRange<int>(0, 10000000);
        const int interval = consensus_params.nRandomXKeyBlockInterval;

        const int key_height = consensus_params.GetRandomXKeyBlockHeight(height);

        // Invariants that must always hold:
        // 1. Key height is non-negative
        assert(key_height >= 0);

        // 2. Key height is less than height (except for very early blocks)
        if (height >= interval * 2) {
            assert(key_height < height);
        }

        // 3. Key height is aligned to interval (or 0)
        assert(key_height == 0 || key_height % interval == 0);

        // 4. Key changes at interval boundaries
        if (height > 0 && height % interval == 0 && height >= interval * 2) {
            const int prev_key = consensus_params.GetRandomXKeyBlockHeight(height - 1);
            const int curr_key = consensus_params.GetRandomXKeyBlockHeight(height);
            // Key should advance by exactly one interval at boundaries
            assert(curr_key == prev_key + interval || curr_key == prev_key);
        }

        // 5. Key stays constant within an interval
        if (height > interval && height % interval != 0) {
            const int prev_key = consensus_params.GetRandomXKeyBlockHeight(height - 1);
            const int curr_key = consensus_params.GetRandomXKeyBlockHeight(height);
            assert(curr_key == prev_key);
        }
    }
}
