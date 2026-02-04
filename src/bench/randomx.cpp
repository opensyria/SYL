// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <chainparams.h>
#include <common/args.h>
#include <crypto/randomx_context.h>
#include <pow.h>
#include <primitives/block.h>
#include <serialize.h>
#include <streams.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <cstring>
#include <vector>

/**
 * RandomX Proof-of-Work Benchmarks
 *
 * These benchmarks measure the performance of RandomX hashing used for
 * OpenSY's ASIC-resistant proof-of-work. Two modes are benchmarked:
 *
 * 1. Light mode (~256KB cache): Used for block validation
 *    - Slower per-hash but low memory footprint
 *    - Suitable for full nodes that only validate
 *
 * 2. Full mode (~2GB dataset): Used for mining
 *    - Much faster per-hash but requires significant memory
 *    - Each mining thread needs its own VM but shares the dataset
 *
 * Expected performance (modern CPU):
 *   Light mode: ~10-15 H/s (validation)
 *   Full mode:  ~2000-4000 H/s per thread (mining)
 */

// Pre-serialized block header for benchmarking (80 bytes)
static std::vector<unsigned char> GetBenchBlockHeader()
{
    CBlockHeader header;
    header.nVersion = 0x20000000;
    header.hashPrevBlock.SetNull();
    header.hashMerkleRoot.SetNull();
    header.nTime = 1733788800;  // Dec 10, 2025
    header.nBits = 0x1e00ffff;
    header.nNonce = 0;

    DataStream ss{};
    ss << header;
    std::vector<unsigned char> result(ss.size());
    std::memcpy(result.data(), ss.data(), ss.size());
    return result;
}

/**
 * Benchmark RandomX light mode (validation)
 *
 * This measures the hash rate achievable when validating blocks.
 * Light mode uses a 256KB cache and is suitable for nodes that
 * don't mine but need to validate incoming blocks.
 */
static void RandomXLightMode(benchmark::Bench& bench)
{
    RandomXContext ctx;
    uint256 keyHash;
    keyHash.SetNull();
    if (!ctx.Initialize(keyHash)) {
        return; // Skip if RandomX init fails
    }

    std::vector<unsigned char> header_data = GetBenchBlockHeader();

    bench.unit("hash").run([&] {
        uint256 hash = ctx.CalculateHash(header_data);
        ankerl::nanobench::doNotOptimizeAway(hash);
    });
}

/**
 * Benchmark RandomX context initialization
 *
 * Measures the time to initialize a RandomX context with a new key.
 * This happens when the key block changes (every 32 blocks on mainnet).
 * Light mode initialization should take ~100-500ms.
 */
static void RandomXContextInit(benchmark::Bench& bench)
{
    uint256 keyHash;
    keyHash.SetNull();

    bench.unit("init").run([&] {
        RandomXContext ctx;
        bool result = ctx.Initialize(keyHash);
        ankerl::nanobench::doNotOptimizeAway(result);
    });
}

/**
 * Benchmark RandomX with varying nonces
 *
 * Simulates actual mining where the nonce is incremented each hash.
 * This ensures the benchmark reflects real-world mining performance.
 */
static void RandomXMiningSimulation(benchmark::Bench& bench)
{
    RandomXContext ctx;
    uint256 keyHash;
    keyHash.SetNull();
    if (!ctx.Initialize(keyHash)) {
        return;
    }

    CBlockHeader header;
    header.nVersion = 0x20000000;
    header.hashPrevBlock.SetNull();
    header.hashMerkleRoot.SetNull();
    header.nTime = 1733788800;
    header.nBits = 0x1e00ffff;
    header.nNonce = 0;

    // Pre-serialize everything except nonce position
    DataStream ss{};
    ss << header;
    std::vector<unsigned char> header_data(ss.size());
    std::memcpy(header_data.data(), ss.data(), ss.size());

    // Nonce is last 4 bytes of 80-byte header
    constexpr size_t NONCE_OFFSET = 76;
    uint32_t nonce = 0;

    bench.unit("hash").run([&] {
        // Update nonce in serialized data (little-endian)
        header_data[NONCE_OFFSET] = nonce & 0xFF;
        header_data[NONCE_OFFSET + 1] = (nonce >> 8) & 0xFF;
        header_data[NONCE_OFFSET + 2] = (nonce >> 16) & 0xFF;
        header_data[NONCE_OFFSET + 3] = (nonce >> 24) & 0xFF;

        uint256 hash = ctx.CalculateHash(header_data);
        ankerl::nanobench::doNotOptimizeAway(hash);

        ++nonce;
    });
}

/**
 * Benchmark CalculateRandomXHash function (full validation path)
 *
 * This benchmarks the complete validation path including header serialization,
 * which is what actually runs during block validation.
 */
static void RandomXValidationPath(benchmark::Bench& bench)
{
    CBlockHeader header;
    header.nVersion = 0x20000000;
    header.hashPrevBlock.SetNull();
    header.hashMerkleRoot.SetNull();
    header.nTime = 1733788800;
    header.nBits = 0x1e00ffff;
    header.nNonce = 12345;

    uint256 keyHash;
    keyHash.SetNull();

    bench.unit("hash").run([&] {
        uint256 hash = CalculateRandomXHash(header, keyHash);
        ankerl::nanobench::doNotOptimizeAway(hash);
    });
}

/**
 * Benchmark key rotation overhead
 *
 * Measures the overhead of switching between different RandomX keys.
 * This simulates validating blocks across key rotation boundaries.
 */
static void RandomXKeyRotation(benchmark::Bench& bench)
{
    uint256 key1;
    key1.SetNull();
    key1.data()[0] = 0x11;

    uint256 key2;
    key2.SetNull();
    key2.data()[0] = 0x22;

    CBlockHeader header;
    header.nVersion = 0x20000000;
    header.hashPrevBlock.SetNull();
    header.hashMerkleRoot.SetNull();
    header.nTime = 1733788800;
    header.nBits = 0x1e00ffff;
    header.nNonce = 0;

    bool use_key1 = true;

    bench.unit("hash").run([&] {
        // Alternate keys to force context reinitialization
        const uint256& key = use_key1 ? key1 : key2;
        uint256 hash = CalculateRandomXHash(header, key);
        ankerl::nanobench::doNotOptimizeAway(hash);
        use_key1 = !use_key1;
    });
}

BENCHMARK(RandomXLightMode, benchmark::PriorityLevel::HIGH);
BENCHMARK(RandomXContextInit, benchmark::PriorityLevel::HIGH);
BENCHMARK(RandomXMiningSimulation, benchmark::PriorityLevel::HIGH);
BENCHMARK(RandomXValidationPath, benchmark::PriorityLevel::HIGH);
BENCHMARK(RandomXKeyRotation, benchmark::PriorityLevel::LOW);

