// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/params.h>
#include <crypto/argon2_context.h>
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

void initialize_argon2_fuzz()
{
    SelectParams(ChainType::REGTEST);
}

/**
 * Fuzz the Argon2id CalculateHash with varying inputs and salts.
 *
 * Uses MINIMUM memory (8 KiB) and time cost to keep execution fast.
 * The goal is to exercise serialization, edge-case inputs (empty, max-length),
 * and ensure no crashes or UB regardless of input.
 */
FUZZ_TARGET(argon2_hash, .init = initialize_argon2_fuzz)
{
    FuzzedDataProvider fuzzed_data(buffer.data(), buffer.size());

    // Use minimal Argon2 parameters for speed (consensus values are 2 GB).
    // 8 KiB is the minimum allowed by the Argon2 spec for parallelism=1.
    constexpr uint32_t FUZZ_MEMORY_COST = 8;  // 8 KiB – fast enough for fuzzing
    constexpr uint32_t FUZZ_TIME_COST   = 1;
    constexpr uint32_t FUZZ_PARALLELISM = 1;

    Argon2Context ctx(FUZZ_MEMORY_COST, FUZZ_TIME_COST, FUZZ_PARALLELISM);

    // Consume a salt (uint256)
    const std::optional<uint256> salt = ConsumeDeserializable<uint256>(fuzzed_data);
    if (!salt) return;

    LIMITED_WHILE(fuzzed_data.remaining_bytes() > 0, 32) {
        const size_t input_size = fuzzed_data.ConsumeIntegralInRange<size_t>(0, 512);
        const std::vector<unsigned char> input = fuzzed_data.ConsumeBytes<unsigned char>(input_size);

        try {
            const uint256 hash = ctx.CalculateHash(input, *salt);
            // Hash must be 32 bytes
            assert(hash.size() == 32);
        } catch (const std::runtime_error&) {
            // Acceptable – context may fail with extreme parameters
        }
    }
}

/**
 * Fuzz the Argon2id CalculateBlockHash path that will be used in
 * actual PoW validation if the emergency Argon2 mode is activated.
 */
FUZZ_TARGET(argon2_block_hash, .init = initialize_argon2_fuzz)
{
    FuzzedDataProvider fuzzed_data(buffer.data(), buffer.size());

    const std::optional<CBlockHeader> header = ConsumeDeserializable<CBlockHeader>(fuzzed_data);
    if (!header) return;

    // Minimal parameters for speed
    Argon2Context ctx(8, 1, 1);

    try {
        const uint256 hash = ctx.CalculateBlockHash(*header);
        assert(hash.size() == 32);

        // Determinism: same header must produce the same hash
        const uint256 hash2 = ctx.CalculateBlockHash(*header);
        assert(hash == hash2);
    } catch (const std::runtime_error&) {
        // Acceptable
    }
}

/**
 * Fuzz the Argon2 parameter boundary conditions.
 * Ensures the constructor and hashing don't crash with varied parameters.
 */
FUZZ_TARGET(argon2_params, .init = initialize_argon2_fuzz)
{
    FuzzedDataProvider fuzzed_data(buffer.data(), buffer.size());

    // Fuzz parameter values – clamp to safe ranges to avoid OOM
    const uint32_t memory_cost = fuzzed_data.ConsumeIntegralInRange<uint32_t>(8, 1024);
    const uint32_t time_cost   = fuzzed_data.ConsumeIntegralInRange<uint32_t>(1, 3);
    const uint32_t parallelism = fuzzed_data.ConsumeIntegralInRange<uint32_t>(1, 4);

    Argon2Context ctx(memory_cost, time_cost, parallelism);

    // Verify accessors
    assert(ctx.GetMemoryCost()  == memory_cost);
    assert(ctx.GetTimeCost()    == time_cost);
    assert(ctx.GetParallelism() == parallelism);

    const std::optional<uint256> salt = ConsumeDeserializable<uint256>(fuzzed_data);
    if (!salt) return;

    const size_t input_size = fuzzed_data.ConsumeIntegralInRange<size_t>(0, 256);
    const std::vector<unsigned char> input = fuzzed_data.ConsumeBytes<unsigned char>(input_size);

    try {
        const uint256 hash = ctx.CalculateHash(input, *salt);
        assert(hash.size() == 32);
    } catch (const std::runtime_error&) {
        // Acceptable
    }
}
