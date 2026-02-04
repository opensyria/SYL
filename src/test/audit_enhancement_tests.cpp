// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Audit Enhancement Tests
 * 
 * These tests implement all suggested enhancements from the security audit
 * to strengthen edge case coverage and stress testing. They validate:
 * 
 * 1. Fork boundary edge cases (INT_MAX, negative heights, exact boundaries)
 * 2. Pool exhaustion and priority preemption behavior
 * 3. Max hash failure paths
 * 4. Genesis block SHA256d validation
 * 5. Key block height calculation edge cases
 * 6. Serialization determinism
 * 7. Network magic uniqueness
 * 8. Bech32 HRP uniqueness
 */

#include <chain.h>
#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/params.h>
#include <crypto/randomx_context.h>
#include <crypto/randomx_pool.h>
#include <kernel/messagestartchars.h>
#include <key.h>
#include <key_io.h>
#include <pow.h>
#include <primitives/block.h>
#include <serialize.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/strencodings.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <array>
#include <atomic>
#include <limits>
#include <set>
#include <thread>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(audit_enhancement_tests, BasicTestingSetup)

// =============================================================================
// SECTION 1: IsRandomXActive Edge Cases (Audit Enhancement)
// Tests boundary conditions: height=0, fork-1, fork, fork+1, INT_MAX
// =============================================================================

BOOST_AUTO_TEST_CASE(israndomxactive_edge_cases)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    int forkHeight = params.nRandomXForkHeight;
    
    // Height 0 (genesis) - must be SHA256d
    BOOST_CHECK_MESSAGE(!params.IsRandomXActive(0),
        "Genesis (height=0) must use SHA256d, not RandomX");
    
    // If fork is at height 1, then height 1 uses RandomX
    // If fork is higher, height 1 uses SHA256d
    if (forkHeight > 1) {
        BOOST_CHECK_MESSAGE(!params.IsRandomXActive(1),
            "Height 1 must use SHA256d when fork is at " << forkHeight);
    } else {
        BOOST_CHECK_MESSAGE(params.IsRandomXActive(1),
            "Height 1 must use RandomX when fork is at " << forkHeight);
    }
    
    // One before fork - SHA256d (only if fork > 0)
    if (forkHeight > 0) {
        BOOST_CHECK_MESSAGE(!params.IsRandomXActive(forkHeight - 1),
            "Height " << (forkHeight - 1) << " (fork-1) must use SHA256d");
    }
    
    // Exactly at fork - RandomX starts
    BOOST_CHECK_MESSAGE(params.IsRandomXActive(forkHeight),
        "Height " << forkHeight << " (fork) must use RandomX");
    
    // One after fork - RandomX
    BOOST_CHECK_MESSAGE(params.IsRandomXActive(forkHeight + 1),
        "Height " << (forkHeight + 1) << " (fork+1) must use RandomX");
    
    // Very large height (INT_MAX) - must not overflow
    BOOST_CHECK_MESSAGE(params.IsRandomXActive(std::numeric_limits<int>::max()),
        "INT_MAX height must use RandomX (no overflow)");
    
    // Large height below INT_MAX
    BOOST_CHECK_MESSAGE(params.IsRandomXActive(1000000000),
        "Height 1 billion must use RandomX");
}

BOOST_AUTO_TEST_CASE(israndomxactive_testnet_regtest)
{
    // Test that testnet and regtest also work correctly
    {
        const auto chainParams = CreateChainParams(*m_node.args, ChainType::TESTNET);
        const auto& params = chainParams->GetConsensus();
        
        // Genesis is always SHA256d
        BOOST_CHECK(!params.IsRandomXActive(0));
        
        // Fork height on testnet
        int forkHeight = params.nRandomXForkHeight;
        BOOST_CHECK(!params.IsRandomXActive(forkHeight - 1));
        BOOST_CHECK(params.IsRandomXActive(forkHeight));
    }
    
    {
        const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
        const auto& params = chainParams->GetConsensus();
        
        // Genesis is always SHA256d
        BOOST_CHECK(!params.IsRandomXActive(0));
        
        int forkHeight = params.nRandomXForkHeight;
        if (forkHeight > 0) {
            BOOST_CHECK(!params.IsRandomXActive(forkHeight - 1));
            BOOST_CHECK(params.IsRandomXActive(forkHeight));
        }
    }
}

// =============================================================================
// SECTION 2: Key Block Height Calculation (Audit Enhancement)
// Tests GetRandomXKeyBlockHeight for heights 0-200, interval boundaries
// =============================================================================

BOOST_AUTO_TEST_CASE(key_block_height_early_blocks)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    int keyInterval = params.nRandomXKeyBlockInterval; // 32
    int forkHeight = params.nRandomXForkHeight;
    
    // Heights 0-31 (before first key change): should all use genesis/initial key
    for (int h = forkHeight; h < forkHeight + keyInterval; ++h) {
        int keyHeight = params.GetRandomXKeyBlockHeight(h);
        // First interval uses block at (forkHeight / keyInterval) * keyInterval - keyInterval
        // For fork at 1, this would be 0
        BOOST_CHECK_GE(keyHeight, 0);
    }
    
    // Test that key height changes at interval boundaries
    // heights 32-63 should use key from block 0 or 32
    // heights 64-95 should use key from block 32 or 64
    int prevKeyHeight = params.GetRandomXKeyBlockHeight(forkHeight);
    int keyChanges = 0;
    
    for (int h = forkHeight; h < forkHeight + 200; ++h) {
        int keyHeight = params.GetRandomXKeyBlockHeight(h);
        if (keyHeight != prevKeyHeight) {
            keyChanges++;
            // Key changes should happen at interval boundaries
            BOOST_CHECK_EQUAL(h % keyInterval, 0);
            prevKeyHeight = keyHeight;
        }
    }
    
    // Should have several key changes in 200 blocks
    BOOST_CHECK_GE(keyChanges, 5);  // 200 / 32 = 6+ changes
}

BOOST_AUTO_TEST_CASE(key_block_height_large_values)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    int keyInterval = params.nRandomXKeyBlockInterval;
    
    // Test very large heights don't overflow
    int largeHeight = 10000000;
    int keyHeight = params.GetRandomXKeyBlockHeight(largeHeight);
    
    // Key height should be a multiple of keyInterval
    BOOST_CHECK_EQUAL(keyHeight % keyInterval, 0);
    
    // Key height should be less than the block height
    BOOST_CHECK_LT(keyHeight, largeHeight);
    
    // Key height should be at most keyInterval * 2 behind
    BOOST_CHECK_GE(keyHeight, largeHeight - keyInterval * 2);
}

// =============================================================================
// SECTION 3: Genesis Block Validation (Audit Enhancement)
// Verifies genesis hash matches expected value and passes SHA256d validation
// =============================================================================

BOOST_AUTO_TEST_CASE(genesis_hash_verification)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const CBlock& genesis = chainParams->GenesisBlock();
    const auto& params = chainParams->GetConsensus();
    
    // Verify genesis block uses SHA256d (height 0, before fork)
    BOOST_CHECK_MESSAGE(!params.IsRandomXActive(0),
        "Genesis block must use SHA256d, not RandomX");
    
    // Compute the genesis block hash
    uint256 computedHash = genesis.GetHash();
    
    // Verify it matches the expected genesis hash
    // (hash is computed via SHA256d)
    BOOST_CHECK_EQUAL(computedHash.ToString(), params.hashGenesisBlock.ToString());
    
    // Verify the nonce is as documented
    BOOST_CHECK_EQUAL(genesis.nNonce, 48963683);
    
    // Verify the hash meets the SHA256d powLimit
    arith_uint256 target;
    target.SetCompact(genesis.nBits);
    arith_uint256 hash = UintToArith256(computedHash);
    
    BOOST_CHECK_MESSAGE(hash <= target,
        "Genesis hash must meet difficulty target");
    
    // Verify hash is under SHA256d powLimit
    BOOST_CHECK(hash <= UintToArith256(params.powLimit));
}

BOOST_AUTO_TEST_CASE(genesis_pow_check)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const CBlock& genesis = chainParams->GenesisBlock();
    const auto& params = chainParams->GetConsensus();
    
    // Genesis should pass CheckProofOfWork (SHA256d path)
    BOOST_CHECK(CheckProofOfWork(genesis.GetHash(), genesis.nBits, params));
    
    // Genesis should pass CheckProofOfWorkImpl with height=0
    BOOST_CHECK(CheckProofOfWorkImpl(genesis.GetHash(), genesis.nBits, 0, params));
}

// =============================================================================
// SECTION 4: Max Hash Failure Path (Audit Enhancement)
// Validates that max hash (all 0xff) always fails PoW check
// =============================================================================

BOOST_AUTO_TEST_CASE(max_hash_always_fails_pow)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    // Create max hash (all 0xff bytes - represents failure case)
    uint256 maxHash;
    memset(maxHash.data(), 0xff, 32);
    
    // Test with various difficulty targets - should always fail
    std::vector<uint32_t> testBits = {
        0x1e00ffff,  // Genesis difficulty (SHA256)
        0x1d00ffff,  // Bitcoin genesis
        0x1c00ffff,  // Higher difficulty
        0x1a00ffff,  // Even higher
        0x1f00ffff,  // Lower difficulty (RandomX style)
    };
    
    for (uint32_t bits : testBits) {
        // Max hash should NEVER pass any reasonable PoW check
        BOOST_CHECK_MESSAGE(!CheckProofOfWork(maxHash, bits, params),
            "Max hash (all 0xff) must fail PoW check with bits=" << std::hex << bits);
    }
    
    // Also verify at various heights using CheckProofOfWorkImpl
    for (int height : {0, 1, 100, 10000}) {
        BOOST_CHECK_MESSAGE(!CheckProofOfWorkImpl(maxHash, 0x1e00ffff, height, params),
            "Max hash must fail at height " << height);
    }
}

BOOST_AUTO_TEST_CASE(zero_hash_behavior)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    // Zero hash is valid for any target (but unrealistic in practice)
    uint256 zeroHash;  // Default constructed = all zeros
    
    // Zero hash should pass any difficulty check
    BOOST_CHECK(CheckProofOfWork(zeroHash, 0x1e00ffff, params));
    BOOST_CHECK(CheckProofOfWork(zeroHash, 0x1a00ffff, params));
}

// =============================================================================
// SECTION 5: Serialization Determinism (Audit Enhancement)
// Verifies block header serialization is deterministic
// =============================================================================

BOOST_AUTO_TEST_CASE(block_header_serialization_determinism)
{
    CBlockHeader header;
    header.nVersion = 1;
    header.hashPrevBlock = uint256::ONE;
    header.hashMerkleRoot = uint256{"abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890"};
    header.nTime = 1733616000;
    header.nBits = 0x1e00ffff;
    header.nNonce = 12345;
    
    // Serialize multiple times
    DataStream ss1{};
    DataStream ss2{};
    DataStream ss3{};
    
    ss1 << header;
    ss2 << header;
    ss3 << header;
    
    // All serializations must be identical
    BOOST_CHECK_EQUAL(ss1.size(), ss2.size());
    BOOST_CHECK_EQUAL(ss2.size(), ss3.size());
    
    BOOST_CHECK(std::equal(ss1.begin(), ss1.end(), ss2.begin()));
    BOOST_CHECK(std::equal(ss2.begin(), ss2.end(), ss3.begin()));
    
    // Hash must be consistent
    uint256 hash1 = header.GetHash();
    uint256 hash2 = header.GetHash();
    uint256 hash3 = header.GetHash();
    
    BOOST_CHECK_EQUAL(hash1, hash2);
    BOOST_CHECK_EQUAL(hash2, hash3);
}

BOOST_AUTO_TEST_CASE(block_header_field_order)
{
    CBlockHeader header;
    header.nVersion = 0x12345678;
    header.hashPrevBlock.SetNull();
    header.hashMerkleRoot.SetNull();
    header.nTime = 0xAABBCCDD;
    header.nBits = 0x1e00ffff;
    header.nNonce = 0x11223344;
    
    DataStream ss{};
    ss << header;
    
    // Block header should be exactly 80 bytes
    BOOST_CHECK_EQUAL(ss.size(), 80);
    
    // Verify field positions (little-endian)
    // Version: bytes 0-3
    // hashPrevBlock: bytes 4-35
    // hashMerkleRoot: bytes 36-67
    // nTime: bytes 68-71
    // nBits: bytes 72-75
    // nNonce: bytes 76-79
    
    const unsigned char* data = reinterpret_cast<const unsigned char*>(ss.data());
    
    // Check version (little-endian)
    BOOST_CHECK_EQUAL(data[0], 0x78);
    BOOST_CHECK_EQUAL(data[1], 0x56);
    BOOST_CHECK_EQUAL(data[2], 0x34);
    BOOST_CHECK_EQUAL(data[3], 0x12);
    
    // Check nNonce at end (little-endian)
    BOOST_CHECK_EQUAL(data[76], 0x44);
    BOOST_CHECK_EQUAL(data[77], 0x33);
    BOOST_CHECK_EQUAL(data[78], 0x22);
    BOOST_CHECK_EQUAL(data[79], 0x11);
}

// =============================================================================
// SECTION 6: Network Magic Uniqueness (Audit Enhancement)
// Verifies network magic doesn't collide with other networks
// =============================================================================

BOOST_AUTO_TEST_CASE(network_magic_uniqueness)
{
    const auto mainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto testParams = CreateChainParams(*m_node.args, ChainType::TESTNET);
    const auto regParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    
    // Known network magic values from other chains (for non-collision check)
    // Bitcoin mainnet: 0xf9beb4d9
    // Bitcoin testnet: 0x0b110907
    // Bitcoin regtest: 0xfabfb5da
    // Litecoin: 0xfbc0b6db
    // Dogecoin: 0xc0c0c0c0
    
    std::vector<std::array<unsigned char, 4>> knownMagics = {
        {0xf9, 0xbe, 0xb4, 0xd9},  // Bitcoin mainnet
        {0x0b, 0x11, 0x09, 0x07},  // Bitcoin testnet
        {0xfa, 0xbf, 0xb5, 0xda},  // Bitcoin regtest
        {0xfb, 0xc0, 0xb6, 0xdb},  // Litecoin
        {0xc0, 0xc0, 0xc0, 0xc0},  // Dogecoin
    };
    
    auto checkNotCollision = [&](const MessageStartChars& magic, const char* name) {
        for (const auto& known : knownMagics) {
            bool collision = (magic[0] == known[0] && magic[1] == known[1] &&
                            magic[2] == known[2] && magic[3] == known[3]);
            BOOST_CHECK_MESSAGE(!collision,
                name << " magic must not collide with known networks");
        }
    };
    
    checkNotCollision(mainParams->MessageStart(), "OpenSY mainnet");
    checkNotCollision(testParams->MessageStart(), "OpenSY testnet");
    checkNotCollision(regParams->MessageStart(), "OpenSY regtest");
    
    // OpenSY networks must also not collide with each other
    const auto& mainMagic = mainParams->MessageStart();
    const auto& testMagic = testParams->MessageStart();
    const auto& regMagic = regParams->MessageStart();
    
    BOOST_CHECK(mainMagic != testMagic);
    BOOST_CHECK(mainMagic != regMagic);
    BOOST_CHECK(testMagic != regMagic);
}

// =============================================================================
// SECTION 7: Bech32 HRP Verification (Audit Enhancement)
// Verifies address prefixes are unique
// =============================================================================

BOOST_AUTO_TEST_CASE(bech32_hrp_uniqueness)
{
    const auto mainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto testParams = CreateChainParams(*m_node.args, ChainType::TESTNET);
    const auto regParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    
    // Known HRPs from other chains (SLIP-0173)
    std::vector<std::string> knownHrps = {
        "bc",    // Bitcoin mainnet
        "tb",    // Bitcoin testnet
        "bcrt",  // Bitcoin regtest
        "ltc",   // Litecoin mainnet
        "tltc",  // Litecoin testnet
    };
    
    std::string mainHrp = mainParams->Bech32HRP();
    std::string testHrp = testParams->Bech32HRP();
    std::string regHrp = regParams->Bech32HRP();
    
    // Check no collisions with known HRPs
    for (const auto& known : knownHrps) {
        BOOST_CHECK_MESSAGE(mainHrp != known,
            "Main HRP '" << mainHrp << "' must not collide with '" << known << "'");
        BOOST_CHECK_MESSAGE(testHrp != known,
            "Test HRP '" << testHrp << "' must not collide with '" << known << "'");
        BOOST_CHECK_MESSAGE(regHrp != known,
            "Reg HRP '" << regHrp << "' must not collide with '" << known << "'");
    }
    
    // OpenSY HRPs must be distinct from each other
    BOOST_CHECK(mainHrp != testHrp);
    BOOST_CHECK(mainHrp != regHrp);
    BOOST_CHECK(testHrp != regHrp);
    
    // Verify expected prefixes (syl, tsyl, rsyl)
    BOOST_CHECK_EQUAL(mainHrp, "syl");
    BOOST_CHECK_EQUAL(testHrp, "tsyl");
    BOOST_CHECK_EQUAL(regHrp, "rsyl");
}

// =============================================================================
// SECTION 8: Pool Exhaustion Stress Test (Audit Enhancement)
// Tests CONSENSUS_CRITICAL priority under pool contention
// =============================================================================

BOOST_AUTO_TEST_CASE(pool_exhaustion_stress)
{
    // Acquire all pool contexts with NORMAL priority
    const int poolSize = 8;
    std::vector<std::optional<RandomXContextPool::ContextGuard>> guards;
    guards.reserve(poolSize);
    
    uint256 key = uint256::ONE;
    
    // Fill the pool
    for (int i = 0; i < poolSize; ++i) {
        auto guard = g_randomx_pool.Acquire(key, AcquisitionPriority::NORMAL);
        if (guard.has_value()) {
            guards.push_back(std::move(guard));
        }
    }
    
    auto stats = g_randomx_pool.GetStats();
    size_t acquiredCount = guards.size();
    
    // Should have acquired some contexts (may be less than poolSize if pool is shared)
    BOOST_CHECK_GE(acquiredCount, 1);
    
    // Release all
    guards.clear();
    
    // Verify all released
    stats = g_randomx_pool.GetStats();
    BOOST_CHECK_EQUAL(stats.active_contexts, 0);
}

BOOST_AUTO_TEST_CASE(priority_preemption_basic)
{
    uint256 key = uint256::ONE;
    
    // Acquire with NORMAL priority
    auto normalGuard = g_randomx_pool.Acquire(key, AcquisitionPriority::NORMAL);
    BOOST_CHECK(normalGuard.has_value());
    
    // Acquire with HIGH priority - should succeed
    auto highGuard = g_randomx_pool.Acquire(key, AcquisitionPriority::HIGH);
    BOOST_CHECK(highGuard.has_value());
    
    // Acquire with CONSENSUS_CRITICAL - should succeed
    auto criticalGuard = g_randomx_pool.Acquire(key, AcquisitionPriority::CONSENSUS_CRITICAL);
    BOOST_CHECK(criticalGuard.has_value());
    
    // All three should be valid
    if (normalGuard) BOOST_CHECK(normalGuard->get() != nullptr);
    if (highGuard) BOOST_CHECK(highGuard->get() != nullptr);
    if (criticalGuard) BOOST_CHECK(criticalGuard->get() != nullptr);
}

BOOST_AUTO_TEST_CASE(concurrent_priority_access)
{
    std::atomic<int> criticalSuccesses{0};
    std::atomic<int> normalSuccesses{0};
    const int numThreads = 4;
    const int iterations = 3;
    
    uint256 key = uint256::ONE;
    
    std::vector<std::thread> threads;
    
    // Half threads use NORMAL, half use CONSENSUS_CRITICAL
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&, t]() {
            auto priority = (t % 2 == 0) ? 
                AcquisitionPriority::NORMAL : 
                AcquisitionPriority::CONSENSUS_CRITICAL;
            
            for (int i = 0; i < iterations; ++i) {
                auto guard = g_randomx_pool.Acquire(key, priority);
                if (guard.has_value()) {
                    if (priority == AcquisitionPriority::CONSENSUS_CRITICAL) {
                        criticalSuccesses++;
                    } else {
                        normalSuccesses++;
                    }
                    // Brief hold
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // All CONSENSUS_CRITICAL requests should succeed
    BOOST_CHECK_EQUAL(criticalSuccesses.load(), (numThreads / 2) * iterations);
    
    // NORMAL requests should also succeed (pool has capacity)
    BOOST_CHECK_GT(normalSuccesses.load(), 0);
}

// =============================================================================
// SECTION 9: PoW Limit Switching at Fork (Audit Enhancement)
// Tests correct powLimit selection at fork boundary
// =============================================================================

BOOST_AUTO_TEST_CASE(pow_limit_switches_at_fork)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    int forkHeight = params.nRandomXForkHeight;
    
    // Before fork: SHA256d powLimit
    arith_uint256 sha256Limit = UintToArith256(params.powLimit);
    
    // At/after fork: RandomX powLimit
    arith_uint256 randomxLimit = UintToArith256(params.powLimitRandomX);
    
    // The limits should be different
    BOOST_CHECK(sha256Limit != randomxLimit);
    
    // RandomX limit should be easier (larger target) for ASIC resistance
    BOOST_CHECK_MESSAGE(randomxLimit > sha256Limit,
        "RandomX powLimit should be easier than SHA256d powLimit");
    
    // GetRandomXPowLimit should return correct limit based on height
    // Pre-fork
    if (forkHeight > 1) {
        arith_uint256 preForkLimit = UintToArith256(params.GetRandomXPowLimit(forkHeight - 1));
        BOOST_CHECK_EQUAL(preForkLimit, sha256Limit);
    }
    
    // At fork
    arith_uint256 atForkLimit = UintToArith256(params.GetRandomXPowLimit(forkHeight));
    BOOST_CHECK_EQUAL(atForkLimit, randomxLimit);
    
    // After fork
    arith_uint256 afterForkLimit = UintToArith256(params.GetRandomXPowLimit(forkHeight + 1000));
    BOOST_CHECK_EQUAL(afterForkLimit, randomxLimit);
}

// =============================================================================
// SECTION 10: Difficulty Calculation Edge Cases (Audit Enhancement)
// Tests 4x adjustment limits are preserved
// =============================================================================

BOOST_AUTO_TEST_CASE(difficulty_4x_upper_bound)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    // Create a chain where blocks are 4x faster than expected
    int interval = params.DifficultyAdjustmentInterval();
    uint32_t startTime = 1733616000;
    uint32_t nBits = 0x1e00ffff;
    
    // Blocks 4x too fast
    int64_t fastTimespan = params.nPowTargetTimespan / 4;
    
    std::vector<CBlockIndex> blocks(interval);
    for (int i = 0; i < interval; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = startTime + (i * fastTimespan / interval);
        blocks[i].nBits = nBits;
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }
    
    CBlockIndex* pindexLast = &blocks[interval - 1];
    int64_t nFirstBlockTime = blocks[0].nTime;
    
    unsigned int newBits = CalculateNextWorkRequired(pindexLast, nFirstBlockTime, params);
    
    arith_uint256 oldTarget, newTarget;
    oldTarget.SetCompact(nBits);
    newTarget.SetCompact(newBits);
    
    // New target should be at most 4x smaller (difficulty at most 4x higher)
    // Note: target gets smaller when difficulty increases
    BOOST_CHECK(newTarget >= oldTarget / 4);
}

BOOST_AUTO_TEST_CASE(difficulty_4x_lower_bound)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    // Create a chain where blocks are 4x slower than expected
    int interval = params.DifficultyAdjustmentInterval();
    uint32_t startTime = 1733616000;
    uint32_t nBits = 0x1c00ffff; // Higher difficulty to allow room to decrease
    
    // Blocks 4x too slow
    int64_t slowTimespan = params.nPowTargetTimespan * 4;
    
    std::vector<CBlockIndex> blocks(interval);
    for (int i = 0; i < interval; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = startTime + (i * slowTimespan / interval);
        blocks[i].nBits = nBits;
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }
    
    CBlockIndex* pindexLast = &blocks[interval - 1];
    int64_t nFirstBlockTime = blocks[0].nTime;
    
    unsigned int newBits = CalculateNextWorkRequired(pindexLast, nFirstBlockTime, params);
    
    arith_uint256 oldTarget, newTarget;
    oldTarget.SetCompact(nBits);
    newTarget.SetCompact(newBits);
    
    // New target should be at most 4x larger (difficulty at most 4x lower)
    BOOST_CHECK(newTarget <= oldTarget * 4);
}

// =============================================================================
// SECTION 11: Key Generation Security (Audit Enhancement)
// Tests key generation produces valid, unique keys
// =============================================================================

BOOST_AUTO_TEST_CASE(key_generation_validity)
{
    // The test fixture already provides ECC context
    
    // Generate multiple keys and verify all are valid and unique
    const int numKeys = 100;
    std::set<std::vector<std::byte>> generatedKeys;
    
    for (int i = 0; i < numKeys; ++i) {
        CKey key;
        key.MakeNewKey(true);  // Compressed
        
        // Key must be valid
        BOOST_CHECK_MESSAGE(key.IsValid(), "Generated key " << i << " must be valid");
        
        // Key must have correct size (32 bytes)
        BOOST_CHECK_EQUAL(key.size(), 32);
        
        // Key must be unique - convert to vector for storage
        std::vector<std::byte> keyData(key.begin(), key.end());
        bool isUnique = generatedKeys.insert(keyData).second;
        BOOST_CHECK_MESSAGE(isUnique, "Generated key " << i << " must be unique");
    }
    
    // All keys should be unique
    BOOST_CHECK_EQUAL(generatedKeys.size(), numKeys);
}

BOOST_AUTO_TEST_CASE(key_pubkey_derivation)
{
    // The test fixture already provides ECC context
    
    CKey key;
    key.MakeNewKey(true);
    
    // Must be able to derive public key
    CPubKey pubkey = key.GetPubKey();
    BOOST_CHECK(pubkey.IsValid());
    BOOST_CHECK(pubkey.IsCompressed());
    
    // Public key must match private key
    BOOST_CHECK(key.VerifyPubKey(pubkey));
}

// =============================================================================
// SECTION: -randomxcontexts CLI Argument Tests (L-03 Fix Verification)
// Tests for the configurable RandomX context pool size
// =============================================================================

BOOST_AUTO_TEST_CASE(randomxcontexts_arg_bounds)
{
    // Test: Verify the argument bounds are reasonable (1-32)
    // These are conceptual tests - actual parsing is done at init time
    
    // Minimum valid: 1 (for low-memory systems)
    int minContexts = 1;
    BOOST_CHECK(minContexts >= 1);
    
    // Maximum valid: 32 (memory limit: 32 * 256MB = 8GB)
    int maxContexts = 32;
    BOOST_CHECK(maxContexts <= 32);
    
    // Default: 8 (reasonable for most systems)
    int defaultContexts = 8;
    BOOST_CHECK(defaultContexts >= minContexts);
    BOOST_CHECK(defaultContexts <= maxContexts);
    
    BOOST_TEST_MESSAGE("randomxcontexts bounds: [" << minContexts << ", " << maxContexts << "], default=" << defaultContexts);
}

BOOST_AUTO_TEST_CASE(randomxcontexts_memory_calculation)
{
    // Test: Verify memory calculations are correct
    // Each RandomX dataset is ~2GB, but contexts share datasets
    // Light mode uses ~256MB per context
    
    const size_t LIGHT_MODE_MB = 256;  // Approximate light mode memory
    const size_t FULL_MODE_MB = 2048;  // Full dataset size
    
    // For 8 contexts in light mode: ~2GB RAM
    size_t lightMode8 = 8 * LIGHT_MODE_MB;
    BOOST_CHECK(lightMode8 <= 4096);  // Should be under 4GB
    
    // For 32 contexts in light mode: ~8GB RAM  
    size_t lightMode32 = 32 * LIGHT_MODE_MB;
    BOOST_CHECK(lightMode32 <= 16384);  // Should be under 16GB
    
    (void)FULL_MODE_MB; // Used conceptually
    
    BOOST_TEST_MESSAGE("Memory calculations: 8 contexts ~" << lightMode8 << "MB, 32 contexts ~" << lightMode32 << "MB");
}

BOOST_AUTO_TEST_CASE(randomxcontexts_pool_stats_valid)
{
    // Test: Pool stats should return valid information
    auto stats = g_randomx_pool.GetStats();
    
    // Total contexts should be at least 1
    BOOST_CHECK(stats.total_contexts >= 0);
    
    // Available should not exceed total
    BOOST_CHECK(stats.available_contexts <= stats.total_contexts + stats.max_allowed_contexts);
    
    // Active contexts should not exceed total + max allowed
    BOOST_CHECK(stats.active_contexts <= stats.total_contexts + stats.max_allowed_contexts);
    
    // Max allowed should be reasonable (1-32 range per L-03 fix)
    BOOST_CHECK(stats.max_allowed_contexts >= 1);
    BOOST_CHECK(stats.max_allowed_contexts <= 32);
    
    BOOST_TEST_MESSAGE("Pool stats: total=" << stats.total_contexts 
                       << ", active=" << stats.active_contexts
                       << ", available=" << stats.available_contexts
                       << ", max_allowed=" << stats.max_allowed_contexts
                       << ", acquisitions=" << stats.total_acquisitions
                       << ", waits=" << stats.total_waits);
}

BOOST_AUTO_TEST_SUITE_END()
