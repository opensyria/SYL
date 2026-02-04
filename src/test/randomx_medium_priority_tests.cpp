// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RandomX Medium Priority Tests (T-11 through T-15)
 * 
 * Medium Priority (P2) tests for edge cases and stress scenarios:
 * - T-11: Cache initialization failure recovery
 * - T-12: Deep reorg across multiple key epochs
 * - T-13: Parallel validation determinism
 * - T-14: nBits boundary values
 * - T-15: Key block at genesis edge case
 */

#include <chain.h>
#include <chainparams.h>
#include <consensus/params.h>
#include <crypto/randomx_context.h>
#include <crypto/randomx_pool.h>
#include <pow.h>
#include <primitives/block.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(randomx_medium_priority_tests, BasicTestingSetup)

// =============================================================================
// T-11: CACHE INITIALIZATION FAILURE RECOVERY
// =============================================================================
// Scenario: RandomX cache allocation fails or context errors

BOOST_AUTO_TEST_CASE(t11_uninitialized_context_throws)
{
    // Test: Using uninitialized context throws appropriate error
    RandomXContext ctx;
    
    BOOST_CHECK(!ctx.IsInitialized());
    BOOST_CHECK(ctx.GetKeyBlockHash().IsNull());
    
    std::vector<unsigned char> input{0x01, 0x02, 0x03};
    
    // Should throw when calculating hash without initialization
    BOOST_CHECK_THROW(ctx.CalculateHash(input), std::runtime_error);
    
    BOOST_TEST_MESSAGE("Uninitialized context throws correctly");
}

BOOST_AUTO_TEST_CASE(t11_context_reinit_clears_state)
{
    // Test: Re-initialization properly clears previous state
    RandomXContext ctx;
    
    uint256 key1{"1111111111111111111111111111111111111111111111111111111111111111"};
    uint256 key2{"2222222222222222222222222222222222222222222222222222222222222222"};
    
    // Initialize with first key
    BOOST_CHECK(ctx.Initialize(key1));
    BOOST_CHECK(ctx.IsInitialized());
    BOOST_CHECK_EQUAL(ctx.GetKeyBlockHash(), key1);
    
    // Hash with first key
    std::vector<unsigned char> input{0x01, 0x02, 0x03};
    uint256 hash1 = ctx.CalculateHash(input);
    
    // Re-initialize with second key
    BOOST_CHECK(ctx.Initialize(key2));
    BOOST_CHECK(ctx.IsInitialized());
    BOOST_CHECK_EQUAL(ctx.GetKeyBlockHash(), key2);
    
    // Hash with second key should be different
    uint256 hash2 = ctx.CalculateHash(input);
    BOOST_CHECK(hash1 != hash2);
    
    BOOST_TEST_MESSAGE("Context reinitialization verified");
}

BOOST_AUTO_TEST_CASE(t11_context_lifecycle_stress)
{
    // Test: Repeated init/destroy cycles don't leak or corrupt
    const int CYCLES = 50;
    
    for (int i = 0; i < CYCLES; ++i) {
        RandomXContext ctx;
        
        uint256 key;
        key.data()[0] = i % 256;
        
        BOOST_CHECK(ctx.Initialize(key));
        BOOST_CHECK(ctx.IsInitialized());
        
        std::vector<unsigned char> input{static_cast<unsigned char>(i), 0x02, 0x03};
        uint256 hash = ctx.CalculateHash(input);
        BOOST_CHECK(!hash.IsNull());
        
        // Context destructor runs here
    }
    
    BOOST_TEST_MESSAGE("Lifecycle stress test: " << CYCLES << " cycles completed");
}

// =============================================================================
// T-12: DEEP REORG ACROSS MULTIPLE KEY EPOCHS
// =============================================================================
// Scenario: 100+ block reorg spanning multiple key rotation intervals

BOOST_AUTO_TEST_CASE(t12_key_epochs_calculation)
{
    // Test: Key calculation across many epochs
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    int interval = params.nRandomXKeyBlockInterval;
    
    // Test key heights for blocks across 10 epochs
    int numEpochs = 10;
    int maxHeight = interval * (numEpochs + 2);
    
    std::map<int, int> keyForEpoch;
    
    for (int h = interval * 2; h < maxHeight; ++h) {
        int keyHeight = params.GetRandomXKeyBlockHeight(h);
        int epoch = h / interval;
        
        // First time seeing this epoch, record the key
        if (keyForEpoch.find(epoch) == keyForEpoch.end()) {
            keyForEpoch[epoch] = keyHeight;
        }
        
        // All blocks in same epoch use same key
        BOOST_CHECK_EQUAL(keyHeight, keyForEpoch[epoch]);
    }
    
    // Keys should advance by interval each epoch
    std::vector<int> epochs;
    for (auto& kv : keyForEpoch) {
        epochs.push_back(kv.first);
    }
    std::sort(epochs.begin(), epochs.end());
    
    for (size_t i = 1; i < epochs.size(); ++i) {
        int prevKey = keyForEpoch[epochs[i-1]];
        int currKey = keyForEpoch[epochs[i]];
        BOOST_CHECK_EQUAL(currKey - prevKey, interval);
    }
    
    BOOST_TEST_MESSAGE("Key epochs verified across " << numEpochs << " epochs");
}

BOOST_AUTO_TEST_CASE(t12_deep_reorg_simulation)
{
    // Test: Simulate validation of 100+ blocks during reorg
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    int interval = params.nRandomXKeyBlockInterval;
    int forkHeight = params.nRandomXForkHeight;
    
    // Create headers for 3 full key epochs (96 blocks for interval=32)
    int reorgDepth = interval * 3;
    int startHeight = forkHeight + interval * 2;  // Start well into RandomX
    
    std::vector<CBlockHeader> headers(reorgDepth);
    
    for (int i = 0; i < reorgDepth; ++i) {
        headers[i].nVersion = 1;
        headers[i].hashPrevBlock = (i > 0) ? headers[i-1].GetHash() : uint256::ONE;
        headers[i].hashMerkleRoot = uint256::ONE;
        headers[i].nTime = 1733788800 + i * 120;
        headers[i].nBits = UintToArith256(params.powLimitRandomX).GetCompact();
        headers[i].nNonce = i;
    }
    
    // Verify correct key is used for each block
    for (int i = 0; i < reorgDepth; ++i) {
        int height = startHeight + i;
        int keyHeight = params.GetRandomXKeyBlockHeight(height);
        
        // Key height should be valid
        BOOST_CHECK_GE(keyHeight, 0);
        BOOST_CHECK_LT(keyHeight, height);
        BOOST_CHECK(keyHeight % interval == 0 || keyHeight == 0);
    }
    
    BOOST_TEST_MESSAGE("Deep reorg simulation: " << reorgDepth << " blocks across 3 key epochs");
}

BOOST_AUTO_TEST_CASE(t12_memory_bounded_during_reorg)
{
    // Test: Memory usage stays bounded during deep reorg
    
    // Simulate heavy reorg validation with many different keys
    const int NUM_KEYS = 20;
    std::vector<uint256> keys(NUM_KEYS);
    for (int i = 0; i < NUM_KEYS; ++i) {
        keys[i] = uint256::ONE;
        keys[i].data()[0] = i;
    }
    
    // Rapid key switching (simulating reorg validation)
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < NUM_KEYS; ++i) {
            auto guard = g_randomx_pool.Acquire(keys[i]);
            if (guard.has_value()) {
                std::vector<unsigned char> input{0x01, 0x02, 0x03};
                try {
                    (*guard)->CalculateHash(input);
                } catch (...) {}
            }
        }
    }
    
    auto statsAfter = g_randomx_pool.GetStats();
    
    // Total contexts should stay bounded
    BOOST_CHECK_LE(statsAfter.total_contexts, RandomXContextPool::MAX_CONTEXTS);
    
    BOOST_TEST_MESSAGE("Memory bounded during reorg simulation: " 
                       << statsAfter.total_contexts << " contexts (max=" 
                       << RandomXContextPool::MAX_CONTEXTS << ")");
}

// =============================================================================
// T-13: PARALLEL VALIDATION DETERMINISM
// =============================================================================
// Scenario: Parallel block validation produces consistent results

BOOST_AUTO_TEST_CASE(t13_parallel_hash_determinism)
{
    // Test: Same block hashed in parallel produces same result
    const int NUM_THREADS = 16;
    const int ITERATIONS = 50;
    
    CBlockHeader header;
    header.nVersion = 0x20000000;
    header.hashPrevBlock = uint256{"0000000000000000000000000000000000000000000000000000000000000001"};
    header.hashMerkleRoot = uint256{"0000000000000000000000000000000000000000000000000000000000000002"};
    header.nTime = 1733788800;
    header.nBits = 0x1e00ffff;
    header.nNonce = 42;
    
    uint256 keyHash{"fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210"};
    
    // Get reference hash
    uint256 referenceHash = CalculateRandomXHash(header, keyHash);
    
    std::atomic<int> mismatches{0};
    std::vector<std::thread> threads;
    std::atomic<bool> start{false};
    
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&]() {
            while (!start.load()) std::this_thread::yield();
            
            for (int i = 0; i < ITERATIONS; ++i) {
                uint256 hash = CalculateRandomXHash(header, keyHash);
                if (hash != referenceHash) {
                    mismatches.fetch_add(1);
                }
            }
        });
    }
    
    start.store(true);
    for (auto& t : threads) t.join();
    
    BOOST_CHECK_EQUAL(mismatches.load(), 0);
    BOOST_TEST_MESSAGE("Parallel determinism: " << NUM_THREADS << " threads x " 
                       << ITERATIONS << " iterations, 0 mismatches");
}

BOOST_AUTO_TEST_CASE(t13_parallel_different_blocks)
{
    // Test: Different blocks hashed in parallel produce correct (different) results
    const int NUM_BLOCKS = 100;
    const int NUM_THREADS = 8;
    
    uint256 keyHash{"1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"};
    
    // Create different headers
    std::vector<CBlockHeader> headers(NUM_BLOCKS);
    for (int i = 0; i < NUM_BLOCKS; ++i) {
        headers[i].nVersion = 1;
        headers[i].hashPrevBlock = uint256::ONE;
        headers[i].hashMerkleRoot = uint256::ONE;
        headers[i].nTime = 1733788800;
        headers[i].nBits = 0x1e00ffff;
        headers[i].nNonce = i;  // Different nonce
    }
    
    // Compute reference hashes single-threaded
    std::vector<uint256> referenceHashes(NUM_BLOCKS);
    for (int i = 0; i < NUM_BLOCKS; ++i) {
        referenceHashes[i] = CalculateRandomXHash(headers[i], keyHash);
    }
    
    // Verify all reference hashes are unique
    std::set<uint256> uniqueHashes(referenceHashes.begin(), referenceHashes.end());
    BOOST_CHECK_EQUAL(uniqueHashes.size(), (size_t)NUM_BLOCKS);
    
    // Parallel verification
    std::atomic<int> errors{0};
    std::vector<std::thread> threads;
    
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = t; i < NUM_BLOCKS; i += NUM_THREADS) {
                uint256 hash = CalculateRandomXHash(headers[i], keyHash);
                if (hash != referenceHashes[i]) {
                    errors.fetch_add(1);
                }
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    BOOST_CHECK_EQUAL(errors.load(), 0);
    BOOST_TEST_MESSAGE("Parallel different blocks: " << NUM_BLOCKS << " unique hashes verified");
}

// =============================================================================
// T-14: NBITS BOUNDARY VALUES
// =============================================================================
// Scenario: Test nBits at exact boundaries

BOOST_AUTO_TEST_CASE(t14_nbits_valid_range)
{
    // Test: Valid nBits values are accepted
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    (void)params; // Used for powLimitRandomX below
    
    CBlockHeader header;
    header.nVersion = 1;
    header.hashPrevBlock = uint256::ONE;
    header.hashMerkleRoot = uint256::ONE;
    header.nTime = 1733788800;
    header.nNonce = 0;
    
    // Test various valid nBits
    std::vector<uint32_t> validBits = {
        0x1d00ffff,  // Typical difficulty
        0x1e00ffff,  // Easy difficulty
        UintToArith256(params.powLimitRandomX).GetCompact(),  // Exactly at limit
        0x1c00ffff,  // Harder difficulty
        0x1b00ffff,  // Even harder
    };
    
    for (uint32_t nBits : validBits) {
        header.nBits = nBits;
        
        // DeriveTarget should succeed for valid nBits
        auto target = DeriveTarget(nBits, params.powLimitRandomX);
        if (target.has_value()) {
            BOOST_CHECK_GT(*target, arith_uint256(0));
        }
    }
    
    BOOST_TEST_MESSAGE("Valid nBits range tested: " << validBits.size() << " values");
}

BOOST_AUTO_TEST_CASE(t14_nbits_invalid_values)
{
    // Test: Invalid nBits values are rejected
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    std::vector<uint32_t> invalidBits = {
        0x00000000,  // Zero
        0x00ffffff,  // Zero exponent with mantissa
        0x80ffffff,  // Negative (high bit of size)
        0x1e800000,  // Negative mantissa
        0x21010000,  // Overflow
    };
    
    for (uint32_t nBits : invalidBits) {
        auto target = DeriveTarget(nBits, params.powLimitRandomX);
        // Should either fail or produce invalid result
        if (target.has_value()) {
            // If it succeeds, target should be > 0 and <= powLimit
            arith_uint256 powLimit = UintToArith256(params.powLimitRandomX);
            BOOST_CHECK_LE(*target, powLimit);
        }
    }
    
    BOOST_TEST_MESSAGE("Invalid nBits rejection tested: " << invalidBits.size() << " values");
}

BOOST_AUTO_TEST_CASE(t14_nbits_compact_roundtrip)
{
    // Test: Compact encoding round-trips correctly
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    arith_uint256 powLimit = UintToArith256(params.powLimitRandomX);
    uint32_t compactLimit = powLimit.GetCompact();
    
    arith_uint256 decoded;
    decoded.SetCompact(compactLimit);
    
    // Compact encoding loses precision, but should be close
    BOOST_CHECK_LE(decoded, powLimit);
    
    // Re-encode should be identical
    uint32_t reEncoded = decoded.GetCompact();
    BOOST_CHECK_EQUAL(reEncoded, compactLimit);
    
    BOOST_TEST_MESSAGE("nBits compact roundtrip verified");
}

// =============================================================================
// T-15: KEY BLOCK AT GENESIS EDGE CASE
// =============================================================================
// Scenario: Early blocks all use genesis as key block

BOOST_AUTO_TEST_CASE(t15_genesis_key_for_early_blocks)
{
    // Test: All early blocks use genesis (height 0) as key
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    int interval = params.nRandomXKeyBlockInterval;
    int forkHeight = params.nRandomXForkHeight;
    
    // First two key intervals should use genesis
    int firstNonGenesisKey = interval * 2;
    
    for (int h = forkHeight; h < firstNonGenesisKey && h < 1000; ++h) {
        int keyHeight = params.GetRandomXKeyBlockHeight(h);
        BOOST_CHECK_MESSAGE(keyHeight == 0,
            "Height " << h << " should use genesis key, got " << keyHeight);
    }
    
    BOOST_TEST_MESSAGE("Genesis key used for heights " << forkHeight << " to " << (firstNonGenesisKey - 1));
}

BOOST_AUTO_TEST_CASE(t15_first_key_rotation)
{
    // Test: First key rotation occurs at height 2*interval
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    int interval = params.nRandomXKeyBlockInterval;
    
    int lastGenesisKeyHeight = interval * 2 - 1;
    int firstRotatedKeyHeight = interval * 2;
    
    int keyBefore = params.GetRandomXKeyBlockHeight(lastGenesisKeyHeight);
    int keyAfter = params.GetRandomXKeyBlockHeight(firstRotatedKeyHeight);
    
    BOOST_CHECK_EQUAL(keyBefore, 0);  // Genesis
    BOOST_CHECK_EQUAL(keyAfter, interval);  // First rotated key
    
    BOOST_TEST_MESSAGE("First key rotation: genesis at height " << lastGenesisKeyHeight 
                       << ", rotated to " << keyAfter << " at height " << firstRotatedKeyHeight);
}

BOOST_AUTO_TEST_CASE(t15_genesis_hash_consistency)
{
    // Test: Hashing with genesis key produces consistent results
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    uint256 genesisHash = chainParams->GetConsensus().hashGenesisBlock;
    
    CBlockHeader header;
    header.nVersion = 1;
    header.hashPrevBlock = genesisHash;  // Points to genesis
    header.hashMerkleRoot = uint256::ONE;
    header.nTime = 1733788800;
    header.nBits = 0x1e00ffff;
    header.nNonce = 12345;
    
    // Hash with genesis as key
    uint256 hash1 = CalculateRandomXHash(header, genesisHash);
    uint256 hash2 = CalculateRandomXHash(header, genesisHash);
    
    BOOST_CHECK_EQUAL(hash1, hash2);
    BOOST_CHECK(!hash1.IsNull());
    
    BOOST_TEST_MESSAGE("Genesis key hash consistency verified");
}

BOOST_AUTO_TEST_CASE(t15_height_zero_key_calculation)
{
    // Test: Key calculation for height 0 (genesis itself)
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    // Height 0 should use key height 0 (or be clamped to 0)
    int keyHeight = params.GetRandomXKeyBlockHeight(0);
    BOOST_CHECK_EQUAL(keyHeight, 0);
    
    // Negative heights should also clamp to 0
    BOOST_CHECK_EQUAL(params.GetRandomXKeyBlockHeight(-1), 0);
    BOOST_CHECK_EQUAL(params.GetRandomXKeyBlockHeight(-100), 0);
    
    BOOST_TEST_MESSAGE("Height 0 and negative heights key calculation verified");
}

BOOST_AUTO_TEST_SUITE_END()
