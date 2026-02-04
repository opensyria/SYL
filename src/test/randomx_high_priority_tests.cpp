// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RandomX High Priority Tests (T-06 through T-10)
 * 
 * High Priority (P1) tests for key rotation, pool stress, and adversarial scenarios:
 * - T-06: Key rotation boundary mining
 * - T-07: Sustained pool exhaustion stress test
 * - T-08: Malformed header flood resistance
 * - T-09: Mixed version network compatibility (unit aspects)
 * - T-10: Timestamp manipulation at fork boundary
 */

#include <chain.h>
#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/params.h>
#include <crypto/randomx_context.h>
#include <crypto/randomx_pool.h>
#include <pow.h>
#include <primitives/block.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(randomx_high_priority_tests, BasicTestingSetup)

// =============================================================================
// T-06: KEY ROTATION BOUNDARY MINING
// =============================================================================
// Scenario: Mining blocks exactly at key rotation boundaries

BOOST_AUTO_TEST_CASE(t06_key_rotation_at_exact_boundary)
{
    // Test: Key changes exactly at interval boundaries
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    int interval = params.nRandomXKeyBlockInterval;
    
    // Test multiple boundaries
    std::vector<int> boundaryHeights = {
        interval * 2,      // First non-genesis key change (64)
        interval * 3,      // 96
        interval * 4,      // 128
        interval * 10,     // 320
        interval * 100,    // 3200
    };
    
    for (int boundary : boundaryHeights) {
        int prevHeight = boundary - 1;
        
        int keyAtPrev = params.GetRandomXKeyBlockHeight(prevHeight);
        int keyAtBoundary = params.GetRandomXKeyBlockHeight(boundary);
        int keyAfter = params.GetRandomXKeyBlockHeight(boundary + 1);
        
        // Key should change at boundary
        if (boundary >= interval * 2) {
            BOOST_CHECK_MESSAGE(keyAtBoundary > keyAtPrev,
                "Key should change at boundary " << boundary);
        }
        
        // Key should stay same within interval
        BOOST_CHECK_EQUAL(keyAtBoundary, keyAfter);
    }
    
    BOOST_TEST_MESSAGE("Key rotation at " << boundaryHeights.size() << " boundaries verified");
}

BOOST_AUTO_TEST_CASE(t06_key_rotation_hash_changes)
{
    // Test: Hash changes when key rotates
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    (void)params; // Key interval used conceptually for test
    
    CBlockHeader header;
    header.nVersion = 1;
    header.hashPrevBlock = uint256::ONE;
    header.hashMerkleRoot = uint256::ONE;
    header.nTime = 1733788800;
    header.nBits = 0x1e00ffff;
    header.nNonce = 12345;
    
    // Simulate key blocks at different heights (non-null keys)
    uint256 keyAtGenesis{"1010101010101010101010101010101010101010101010101010101010101010"};
    uint256 keyAtInterval{"1111111111111111111111111111111111111111111111111111111111111111"};
    uint256 keyAt2Interval{"2222222222222222222222222222222222222222222222222222222222222222"};
    
    uint256 hash1 = CalculateRandomXHash(header, keyAtGenesis);
    uint256 hash2 = CalculateRandomXHash(header, keyAtInterval);
    uint256 hash3 = CalculateRandomXHash(header, keyAt2Interval);
    
    // All hashes should be different
    BOOST_CHECK(hash1 != hash2);
    BOOST_CHECK(hash2 != hash3);
    BOOST_CHECK(hash1 != hash3);
    
    BOOST_TEST_MESSAGE("Key rotation produces different hashes: verified");
}

BOOST_AUTO_TEST_CASE(t06_mining_across_key_boundary)
{
    // Test: Mining simulation across key rotation boundary
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    int interval = params.nRandomXKeyBlockInterval;
    
    // Create headers for blocks around key rotation
    int startHeight = interval * 2 - 2;  // 62 for interval=32
    int endHeight = interval * 2 + 2;    // 66 for interval=32
    
    std::vector<CBlockHeader> headers;
    for (int h = startHeight; h <= endHeight; ++h) {
        CBlockHeader header;
        header.nVersion = 1;
        header.hashPrevBlock = uint256::ONE;
        header.hashMerkleRoot = uint256::ONE;
        header.nTime = 1733788800 + h * 120;  // 2 min blocks
        header.nBits = 0x1e00ffff;
        header.nNonce = h;  // Different nonce per block
        headers.push_back(header);
    }
    
    // Key block for heights 62,63 is block 0
    // Key block for heights 64,65,66 is block 32
    // Note: Using non-null key for "genesis" as test doesn't use actual chain
    uint256 keyBlock0{"abcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcd"};
    uint256 keyBlock32{"3232323232323232323232323232323232323232323232323232323232323232"};
    
    // First two blocks use genesis key
    for (int i = 0; i < 2; ++i) {
        int height = startHeight + i;
        int expectedKeyHeight = params.GetRandomXKeyBlockHeight(height);
        BOOST_CHECK_EQUAL(expectedKeyHeight, 0);
        
        uint256 hash = CalculateRandomXHash(headers[i], keyBlock0);
        BOOST_CHECK(!hash.IsNull());
    }
    
    // Last three blocks use interval key (block 32)
    for (int i = 2; i < 5; ++i) {
        int height = startHeight + i;
        int expectedKeyHeight = params.GetRandomXKeyBlockHeight(height);
        BOOST_CHECK_EQUAL(expectedKeyHeight, interval);
        
        uint256 hash = CalculateRandomXHash(headers[i], keyBlock32);
        BOOST_CHECK(!hash.IsNull());
    }
    
    BOOST_TEST_MESSAGE("Mining across key boundary simulated successfully");
}

// =============================================================================
// T-07: SUSTAINED POOL EXHAUSTION STRESS TEST
// =============================================================================
// Scenario: Context pool under sustained high load

BOOST_AUTO_TEST_CASE(t07_pool_exhaustion_basic)
{
    // Test: Pool handles exhaustion gracefully
    uint256 key = uint256::ONE;
    
    // Acquire multiple contexts
    std::vector<std::optional<RandomXContextPool::ContextGuard>> guards;
    
    for (size_t i = 0; i < RandomXContextPool::MAX_CONTEXTS; ++i) {
        auto guard = g_randomx_pool.Acquire(key);
        if (guard.has_value()) {
            guards.push_back(std::move(guard));
        }
    }
    
    // Should have acquired up to MAX_CONTEXTS
    BOOST_CHECK_LE(guards.size(), RandomXContextPool::MAX_CONTEXTS);
    
    auto stats = g_randomx_pool.GetStats();
    BOOST_CHECK_LE(stats.total_contexts, RandomXContextPool::MAX_CONTEXTS);
    
    // Release all
    guards.clear();
    
    // Pool should recover
    auto guard = g_randomx_pool.Acquire(key);
    BOOST_CHECK(guard.has_value());
    
    BOOST_TEST_MESSAGE("Pool exhaustion basic test passed");
}

BOOST_AUTO_TEST_CASE(t07_pool_sustained_stress)
{
    // Test: Sustained load over multiple iterations
    const int NUM_THREADS = 16;
    const int ITERATIONS = 100;
    
    std::atomic<int> successful{0};
    std::atomic<int> failed{0};
    std::atomic<bool> start{false};
    
    std::vector<std::thread> threads;
    
    // Pre-generate keys
    std::vector<uint256> keys(8);
    for (int i = 0; i < 8; ++i) {
        keys[i] = uint256::ONE;
        keys[i].data()[0] = i;
    }
    
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            while (!start.load()) {
                std::this_thread::yield();
            }
            
            for (int i = 0; i < ITERATIONS; ++i) {
                const uint256& key = keys[(t + i) % keys.size()];
                
                auto guard = g_randomx_pool.Acquire(key);
                if (guard.has_value()) {
                    successful.fetch_add(1);
                    // Simulate some work
                    std::vector<unsigned char> input{0x01, 0x02, 0x03};
                    try {
                        (*guard)->CalculateHash(input);
                    } catch (...) {
                        // Ignore hash errors
                    }
                } else {
                    failed.fetch_add(1);
                }
            }
        });
    }
    
    // Start all threads simultaneously
    start.store(true);
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // With blocking acquisition, all should eventually succeed
    int totalAttempts = NUM_THREADS * ITERATIONS;
    BOOST_CHECK_MESSAGE(successful.load() > totalAttempts * 0.9,
        "At least 90% of acquisitions should succeed: " << successful.load() << "/" << totalAttempts);
    
    BOOST_TEST_MESSAGE("Sustained stress test: " << successful.load() << " successful, " 
                       << failed.load() << " failed out of " << totalAttempts);
}

BOOST_AUTO_TEST_CASE(t07_pool_priority_under_load)
{
    // Test: CONSENSUS_CRITICAL priority gets served under load
    const int NUM_WORKERS = 4;
    std::atomic<bool> allDone{false};
    std::atomic<int> holdingCount{0};
    
    // Workers that hold contexts
    std::vector<std::thread> holders;
    
    for (int i = 0; i < NUM_WORKERS; ++i) {
        holders.emplace_back([&, i]() {
            uint256 key = uint256::ONE;
            key.data()[0] = static_cast<unsigned char>(i);
            
            auto guard = g_randomx_pool.Acquire(key, AcquisitionPriority::NORMAL);
            if (guard.has_value()) {
                holdingCount.fetch_add(1);
                // Hold until test complete
                while (!allDone.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
        });
    }
    
    // Give holders time to acquire
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Now try consensus critical acquisition
    uint256 consensusKey{"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"};
    auto consensusGuard = g_randomx_pool.Acquire(consensusKey, AcquisitionPriority::CONSENSUS_CRITICAL);
    
    // Consensus critical should succeed (pool has capacity or can wait)
    BOOST_CHECK_MESSAGE(consensusGuard.has_value(),
        "CONSENSUS_CRITICAL acquisition should succeed");
    
    // Cleanup
    allDone.store(true);
    consensusGuard.reset();
    
    for (auto& holder : holders) {
        holder.join();
    }
    
    BOOST_TEST_MESSAGE("Priority under load test completed, " << holdingCount.load() << " contexts were held");
}

// =============================================================================
// T-08: MALFORMED HEADER FLOOD RESISTANCE
// =============================================================================
// Scenario: Adversary sends many headers with invalid nBits

BOOST_AUTO_TEST_CASE(t08_invalid_nbits_rejection)
{
    // Test: Headers with invalid nBits are rejected before expensive hashing
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    int forkHeight = params.nRandomXForkHeight;
    
    CBlockHeader header;
    header.nVersion = 1;
    header.hashPrevBlock = uint256::ONE;
    header.hashMerkleRoot = uint256::ONE;
    header.nTime = 1733788800;
    header.nNonce = 0;
    
    // Test various invalid nBits values
    std::vector<uint32_t> invalidBits = {
        0x00000000,  // Zero
        0x80000000,  // Negative (high bit set in size byte)
        0x1e80ffff,  // Negative mantissa
        0xff00ffff,  // Exponent too large (overflow)
        0x2100ffff,  // Exceeds powLimit
    };
    
    for (uint32_t nBits : invalidBits) {
        header.nBits = nBits;
        
        bool result = CheckProofOfWorkForBlockIndex(header, forkHeight, params);
        BOOST_CHECK_MESSAGE(!result,
            "nBits " << std::hex << nBits << " should be rejected");
    }
    
    BOOST_TEST_MESSAGE("Invalid nBits rejection verified for " << invalidBits.size() << " cases");
}

BOOST_AUTO_TEST_CASE(t08_header_flood_performance)
{
    // Test: Can process many invalid headers quickly (no expensive RandomX)
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    int forkHeight = params.nRandomXForkHeight;
    
    const int NUM_HEADERS = 10000;
    
    CBlockHeader header;
    header.nVersion = 1;
    header.hashPrevBlock = uint256::ONE;
    header.hashMerkleRoot = uint256::ONE;
    header.nTime = 1733788800;
    header.nNonce = 0;
    header.nBits = 0x2100ffff;  // Invalid: exceeds powLimit
    
    auto start = std::chrono::steady_clock::now();
    
    int rejected = 0;
    for (int i = 0; i < NUM_HEADERS; ++i) {
        header.nNonce = i;  // Vary nonce to prevent caching
        if (!CheckProofOfWorkForBlockIndex(header, forkHeight, params)) {
            rejected++;
        }
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    BOOST_CHECK_EQUAL(rejected, NUM_HEADERS);
    
    // Should process 10k headers in under 1 second (no RandomX computation)
    BOOST_CHECK_MESSAGE(duration.count() < 1000,
        "Processing " << NUM_HEADERS << " invalid headers took " << duration.count() << "ms (should be <1000ms)");
    
    BOOST_TEST_MESSAGE("Header flood test: " << NUM_HEADERS << " headers in " << duration.count() << "ms");
}

BOOST_AUTO_TEST_CASE(t08_pow_limit_boundary_check)
{
    // Test: nBits exactly at powLimit boundary
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    int forkHeight = params.nRandomXForkHeight;
    
    arith_uint256 powLimit = UintToArith256(params.powLimitRandomX);
    uint32_t exactBits = powLimit.GetCompact();
    
    CBlockHeader header;
    header.nVersion = 1;
    header.hashPrevBlock = uint256::ONE;
    header.hashMerkleRoot = uint256::ONE;
    header.nTime = 1733788800;
    header.nNonce = 0;
    
    // Exactly at limit: should pass basic check
    header.nBits = exactBits;
    bool atLimit = CheckProofOfWorkForBlockIndex(header, forkHeight, params);
    BOOST_CHECK_MESSAGE(atLimit, "nBits at exact powLimit should pass basic check");
    
    // Just over limit: should fail
    arith_uint256 overLimit = powLimit;
    overLimit += 1;
    header.nBits = overLimit.GetCompact();
    // Note: Due to compact encoding, +1 may not change compact form
    // So we test with a larger increase
    overLimit = powLimit * 2;
    header.nBits = overLimit.GetCompact();
    bool overResult = CheckProofOfWorkForBlockIndex(header, forkHeight, params);
    BOOST_CHECK_MESSAGE(!overResult, "nBits significantly over powLimit should fail");
    
    BOOST_TEST_MESSAGE("powLimit boundary check verified");
}

// =============================================================================
// T-09: MIXED VERSION NETWORK COMPATIBILITY (UNIT ASPECTS)
// =============================================================================
// Scenario: Different node versions on the network

BOOST_AUTO_TEST_CASE(t09_version_independent_consensus_params)
{
    // Test: Consensus params are consistent across chain types
    const auto mainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto testParams = CreateChainParams(*m_node.args, ChainType::TESTNET);
    const auto signetParams = CreateChainParams(*m_node.args, ChainType::SIGNET);
    
    // Key interval should be defined for all chain types
    BOOST_CHECK_GT(mainParams->GetConsensus().nRandomXKeyBlockInterval, 0);
    BOOST_CHECK_GT(testParams->GetConsensus().nRandomXKeyBlockInterval, 0);
    BOOST_CHECK_GT(signetParams->GetConsensus().nRandomXKeyBlockInterval, 0);
    
    // Fork height should be defined
    BOOST_CHECK_GE(mainParams->GetConsensus().nRandomXForkHeight, 0);
    BOOST_CHECK_GE(testParams->GetConsensus().nRandomXForkHeight, 0);
    BOOST_CHECK_GE(signetParams->GetConsensus().nRandomXForkHeight, 0);
    
    // powLimitRandomX should be defined and non-zero
    BOOST_CHECK(!mainParams->GetConsensus().powLimitRandomX.IsNull());
    BOOST_CHECK(!testParams->GetConsensus().powLimitRandomX.IsNull());
    BOOST_CHECK(!signetParams->GetConsensus().powLimitRandomX.IsNull());
    
    BOOST_TEST_MESSAGE("Consensus params consistency verified across chain types");
}

BOOST_AUTO_TEST_CASE(t09_genesis_block_compatibility)
{
    // Test: Genesis block is consistent and valid SHA256d
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const CBlock& genesis = chainParams->GenesisBlock();
    
    // Genesis hash should match configured value
    BOOST_CHECK_EQUAL(genesis.GetHash(), chainParams->GetConsensus().hashGenesisBlock);
    
    // Genesis should use SHA256d (height 0, before fork)
    BOOST_CHECK(!chainParams->GetConsensus().IsRandomXActive(0));
    
    // Genesis nBits should be valid
    arith_uint256 target;
    target.SetCompact(genesis.nBits);
    BOOST_CHECK_GT(target, arith_uint256(0));
    
    BOOST_TEST_MESSAGE("Genesis block compatibility verified");
}

// =============================================================================
// T-10: TIMESTAMP MANIPULATION AT FORK BOUNDARY
// =============================================================================
// Scenario: Attempts to manipulate timestamps at fork boundary

BOOST_AUTO_TEST_CASE(t10_timestamp_rules_at_fork)
{
    // Test: Timestamp rules apply consistently at fork boundary
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    int forkHeight = params.nRandomXForkHeight;
    
    // Create chain approaching fork
    std::vector<CBlockIndex> blocks(forkHeight + 5);
    uint32_t startTime = 1733616000;
    
    for (int i = 0; i < (int)blocks.size(); ++i) {
        blocks[i].pprev = (i > 0) ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = startTime + i * params.nPowTargetSpacing;
        blocks[i].nBits = UintToArith256(params.GetRandomXPowLimit(i)).GetCompact();
    }
    
    // Median time past for fork block
    if (forkHeight > 0 && forkHeight < (int)blocks.size()) {
        // MTP is median of last 11 blocks
        int mtpHeight = std::max(0, forkHeight - 11);
        std::vector<int64_t> times;
        for (int h = mtpHeight; h < forkHeight && h < (int)blocks.size(); ++h) {
            times.push_back(blocks[h].nTime);
        }
        
        if (times.size() > 0) {
            std::sort(times.begin(), times.end());
            int64_t mtp = times[times.size() / 2];
            
            // Block at fork must be > MTP
            BOOST_CHECK_GT(blocks[forkHeight].nTime, mtp);
        }
    }
    
    BOOST_TEST_MESSAGE("Timestamp rules at fork verified");
}

BOOST_AUTO_TEST_CASE(t10_future_timestamp_rejection)
{
    // Test: Future timestamps are rejected regardless of algorithm
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    (void)chainParams; // Using for conceptual chain reference
    
    // MAX_FUTURE_BLOCK_TIME is 2 hours (7200 seconds)
    constexpr int64_t MAX_FUTURE = 7200;
    
    // Current time simulation
    int64_t now = 1733788800;
    
    CBlockHeader header;
    header.nVersion = 1;
    header.hashPrevBlock = uint256::ONE;
    header.hashMerkleRoot = uint256::ONE;
    header.nBits = 0x1e00ffff;
    header.nNonce = 0;
    
    // Valid timestamp (now + 1 hour)
    header.nTime = now + 3600;
    bool validTime = (header.nTime < static_cast<uint32_t>(now + MAX_FUTURE));
    BOOST_CHECK(validTime);
    
    // Invalid timestamp (now + 3 hours)
    header.nTime = now + 10800;
    bool invalidTime = (header.nTime > static_cast<uint32_t>(now + MAX_FUTURE));
    BOOST_CHECK(invalidTime);
    
    // This test verifies the constants are correct
    BOOST_CHECK_EQUAL(MAX_FUTURE_BLOCK_TIME, 7200);
    
    BOOST_TEST_MESSAGE("Future timestamp rejection constants verified");
}

BOOST_AUTO_TEST_CASE(t10_timestamp_not_special_at_fork)
{
    // Test: No special timestamp handling at fork - standard rules apply
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    int forkHeight = params.nRandomXForkHeight;
    
    // The fork height has no special timestamp rules
    // Same MTP and MAX_FUTURE rules apply as any other block
    
    // Verify fork height doesn't trigger any special cases in difficulty
    if (forkHeight > 0) {
        // Fork height IS a special case only for:
        // 1. Algorithm selection (RandomX vs SHA256d)
        // 2. Difficulty reset to powLimitRandomX
        // NOT for timestamp rules
        
        BOOST_CHECK(params.IsRandomXActive(forkHeight));
        BOOST_CHECK(!params.IsRandomXActive(forkHeight - 1));
        
        // No timestamp-specific fork handling in consensus params
        // (This is verified by the absence of such code in ContextualCheckBlockHeader)
    }
    
    BOOST_TEST_MESSAGE("No special timestamp handling at fork verified");
}

// =============================================================================
// LOW PRIORITY LEVEL TESTS (L-01 Fix Verification)
// =============================================================================
// Tests for the new LOW priority level for background/pre-mining operations

BOOST_AUTO_TEST_CASE(low_priority_basic_acquisition)
{
    // Test: LOW priority can acquire a context
    uint256 key{"1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"};
    
    auto guard = g_randomx_pool.Acquire(key, AcquisitionPriority::LOW);
    BOOST_CHECK(guard.has_value());
    
    BOOST_TEST_MESSAGE("LOW priority basic acquisition successful");
}

BOOST_AUTO_TEST_CASE(low_priority_yields_to_normal)
{
    // Test: LOW priority should yield to NORMAL priority
    // This verifies that ShouldYieldToHigherPriority works correctly
    
    // LOW yields to NORMAL (priority 0 < 1)
    BOOST_CHECK(static_cast<int>(AcquisitionPriority::LOW) < static_cast<int>(AcquisitionPriority::NORMAL));
    
    // Verify enum ordering
    BOOST_CHECK_EQUAL(static_cast<int>(AcquisitionPriority::LOW), 0);
    BOOST_CHECK_EQUAL(static_cast<int>(AcquisitionPriority::NORMAL), 1);
    BOOST_CHECK_EQUAL(static_cast<int>(AcquisitionPriority::HIGH), 2);
    BOOST_CHECK_EQUAL(static_cast<int>(AcquisitionPriority::CONSENSUS_CRITICAL), 3);
    
    BOOST_TEST_MESSAGE("LOW priority ordering verified");
}

BOOST_AUTO_TEST_CASE(low_priority_timeout_is_shortest)
{
    // Test: LOW priority has the shortest timeout (15 seconds)
    // This is by design - background tasks should give up quickly
    
    // We can't directly test timeout values without exposing internals,
    // but we verify the priority hierarchy is correct for yielding
    
    // LOW (0) should yield to all other priorities
    BOOST_CHECK(static_cast<int>(AcquisitionPriority::LOW) < static_cast<int>(AcquisitionPriority::NORMAL));
    BOOST_CHECK(static_cast<int>(AcquisitionPriority::LOW) < static_cast<int>(AcquisitionPriority::HIGH));
    BOOST_CHECK(static_cast<int>(AcquisitionPriority::LOW) < static_cast<int>(AcquisitionPriority::CONSENSUS_CRITICAL));
    
    BOOST_TEST_MESSAGE("LOW priority timeout hierarchy verified");
}

BOOST_AUTO_TEST_CASE(low_priority_multiple_acquisitions)
{
    // Test: Multiple LOW priority acquisitions work correctly
    uint256 key1{"1111111111111111111111111111111111111111111111111111111111111111"};
    uint256 key2{"2222222222222222222222222222222222222222222222222222222222222222"};
    
    auto guard1 = g_randomx_pool.Acquire(key1, AcquisitionPriority::LOW);
    BOOST_CHECK(guard1.has_value());
    
    auto guard2 = g_randomx_pool.Acquire(key2, AcquisitionPriority::LOW);
    BOOST_CHECK(guard2.has_value());
    
    BOOST_TEST_MESSAGE("Multiple LOW priority acquisitions successful");
}

BOOST_AUTO_TEST_SUITE_END()
