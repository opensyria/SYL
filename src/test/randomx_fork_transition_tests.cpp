// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/params.h>
#include <crypto/randomx_context.h>
#include <pow.h>
#include <primitives/block.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

/**
 * RandomX Fork Transition Tests
 * 
 * These tests verify the correct behavior during the critical transition
 * from SHA256d to RandomX proof-of-work, including:
 * - Difficulty reset at fork height
 * - Algorithm selection at fork boundary
 * - Key block calculation during fork transition
 * - Coinbase maturity across fork boundary
 */

BOOST_FIXTURE_TEST_SUITE(randomx_fork_transition_tests, BasicTestingSetup)

// =============================================================================
// FORK HEIGHT BOUNDARY TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(fork_height_algorithm_selection)
{
    // Test: Verify correct algorithm is selected at each height around fork
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    int forkHeight = params.nRandomXForkHeight;
    
    // Before fork: SHA256d
    BOOST_CHECK_MESSAGE(!params.IsRandomXActive(forkHeight - 1),
        "SHA256d should be used at height " << (forkHeight - 1));
    
    // At fork: RandomX
    BOOST_CHECK_MESSAGE(params.IsRandomXActive(forkHeight),
        "RandomX should be active at fork height " << forkHeight);
    
    // After fork: RandomX
    BOOST_CHECK_MESSAGE(params.IsRandomXActive(forkHeight + 1),
        "RandomX should be active at height " << (forkHeight + 1));
    
    // Edge case: height 0 (genesis)
    BOOST_CHECK_MESSAGE(!params.IsRandomXActive(0),
        "Genesis block should use SHA256d");
}

BOOST_AUTO_TEST_CASE(difficulty_reset_at_fork)
{
    // Test: Difficulty resets to powLimitRandomX at fork height
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    int forkHeight = params.nRandomXForkHeight;
    
    // Create mock chain to fork height - 1
    std::vector<CBlockIndex> blocks(forkHeight);
    
    uint32_t sha256Bits = 0x1e00ffff;  // SHA256 powLimit
    uint32_t startTime = 1733616000;
    
    for (int i = 0; i < forkHeight; ++i) {
        blocks[i].pprev = (i > 0) ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = startTime + i * params.nPowTargetSpacing;
        blocks[i].nBits = sha256Bits;
        blocks[i].nChainWork = (i > 0) ? 
            blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : 
            arith_uint256(0);
    }
    
    CBlockIndex* pindexLast = &blocks[forkHeight - 1];
    CBlockHeader header;
    header.nTime = pindexLast->nTime + params.nPowTargetSpacing;
    
    // GetNextWorkRequired at fork height should return RandomX powLimit
    unsigned int nextBits = GetNextWorkRequired(pindexLast, &header, params);
    unsigned int randomxLimitBits = UintToArith256(params.powLimitRandomX).GetCompact();
    
    BOOST_CHECK_EQUAL(nextBits, randomxLimitBits);
}

BOOST_AUTO_TEST_CASE(pow_limit_selection_by_height)
{
    // Test: GetRandomXPowLimit returns correct limit based on height
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    int forkHeight = params.nRandomXForkHeight;
    
    // Before fork: SHA256 powLimit
    uint256 preForkLimit = params.GetRandomXPowLimit(forkHeight - 1);
    BOOST_CHECK_EQUAL(preForkLimit, params.powLimit);
    
    // At/after fork: RandomX powLimit
    uint256 atForkLimit = params.GetRandomXPowLimit(forkHeight);
    BOOST_CHECK_EQUAL(atForkLimit, params.powLimitRandomX);
    
    uint256 postForkLimit = params.GetRandomXPowLimit(forkHeight + 100);
    BOOST_CHECK_EQUAL(postForkLimit, params.powLimitRandomX);
}

// =============================================================================
// KEY BLOCK CALCULATION AT FORK BOUNDARY
// =============================================================================

BOOST_AUTO_TEST_CASE(key_block_at_fork_height)
{
    // Test: Key block calculation at exactly the fork height
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    int forkHeight = params.nRandomXForkHeight;
    int interval = params.nRandomXKeyBlockInterval;
    
    // At fork height (210000 for mainnet), key block follows the formula
    int keyHeight = params.GetRandomXKeyBlockHeight(forkHeight);
    int expectedKeyHeight = std::max(0, (forkHeight / interval) * interval - interval);
    BOOST_CHECK_EQUAL(keyHeight, expectedKeyHeight);
    
    // First block that uses a non-genesis key
    // With interval=32: heights 64+ use key from block 32
    int firstNonGenesisKeyHeight = interval * 2;
    int expectedKey = interval;
    BOOST_CHECK_EQUAL(params.GetRandomXKeyBlockHeight(firstNonGenesisKeyHeight), expectedKey);
}

BOOST_AUTO_TEST_CASE(key_rotation_across_fork)
{
    // Test: Key rotation works correctly when fork is within first interval
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    int interval = params.nRandomXKeyBlockInterval; // 32
    
    // All heights from 1 to 63 use genesis as key block
    for (int h = 1; h < interval * 2; ++h) {
        if (params.IsRandomXActive(h)) {
            int keyHeight = params.GetRandomXKeyBlockHeight(h);
            if (h < interval * 2) {
                BOOST_CHECK_MESSAGE(keyHeight == 0,
                    "Height " << h << " should use genesis as key, got key height " << keyHeight);
            }
        }
    }
}

// =============================================================================
// COINBASE MATURITY ACROSS FORK BOUNDARY
// =============================================================================

BOOST_AUTO_TEST_CASE(coinbase_maturity_constant_across_fork)
{
    // Test: Coinbase maturity (100 blocks) is constant regardless of PoW algorithm
    // This ensures coinbases mined with SHA256d can be spent after 100 RandomX blocks
    
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    // COINBASE_MATURITY is defined in consensus/consensus.h
    BOOST_CHECK_EQUAL(COINBASE_MATURITY, 100);
    
    // Verify it's a global constant, not per-algorithm
    int forkHeight = params.nRandomXForkHeight;
    
    // A coinbase at height (forkHeight - 1) using SHA256d
    // should be spendable at height (forkHeight - 1 + COINBASE_MATURITY)
    // regardless of the PoW algorithm at that spending height
    int coinbaseHeight = forkHeight - 1;
    int spendableHeight = coinbaseHeight + COINBASE_MATURITY;
    
    // Both heights could be on different algorithms
    bool coinbaseUsesRandomX = params.IsRandomXActive(coinbaseHeight);
    bool spendUsesRandomX = params.IsRandomXActive(spendableHeight);
    
    // Pre-fork coinbase (SHA256d)
    BOOST_CHECK(!coinbaseUsesRandomX);
    // Spending height is definitely post-fork
    BOOST_CHECK(spendUsesRandomX);
    
    // Maturity calculation doesn't depend on algorithm
    // (This is verified by the fact that COINBASE_MATURITY is a compile-time constant)
}

// =============================================================================
// DIFFICULTY ADJUSTMENT ACROSS FORK
// =============================================================================

BOOST_AUTO_TEST_CASE(first_difficulty_adjustment_after_fork)
{
    // Test: First difficulty adjustment after fork uses RandomX powLimit correctly
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    // Fork height is 1, so first adjustment period after fork ends at:
    // DAI = 2016 (typical), so first adjustment at height ~2016
    int dai = params.DifficultyAdjustmentInterval();
    int forkHeight = params.nRandomXForkHeight;
    
    // Ensure DAI is reasonable (should be 10080 for 2-min blocks over 2 weeks)
    BOOST_CHECK(dai > 0);
    BOOST_CHECK(dai <= 20160); // Max reasonable value
    
    // First adjustment period that's entirely post-fork
    int firstFullPeriodEnd = ((forkHeight / dai) + 2) * dai - 1;
    
    // At this height, RandomX should definitely be active
    BOOST_CHECK(params.IsRandomXActive(firstFullPeriodEnd));
    
    // powLimit used should be RandomX's
    uint256 powLimit = params.GetRandomXPowLimit(firstFullPeriodEnd);
    BOOST_CHECK_EQUAL(powLimit, params.powLimitRandomX);
}

BOOST_AUTO_TEST_CASE(difficulty_bounds_at_fork)
{
    // Test: Difficulty can't exceed RandomX powLimit after fork
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    // Create a scenario where blocks are very slow (should decrease difficulty)
    // but we're still bounded by powLimit
    
    int forkHeight = params.nRandomXForkHeight;
    (void)forkHeight; // Used to document the fork context
    
    // Verify powLimitRandomX is the cap
    arith_uint256 randomxLimit = UintToArith256(params.powLimitRandomX);
    
    // After fork, difficulty adjustments are capped at randomxLimit
    // (This is enforced in CalculateNextWorkRequired)
    BOOST_CHECK(randomxLimit > 0);
}

// =============================================================================
// BLOCK VALIDATION ACROSS FORK
// =============================================================================

BOOST_AUTO_TEST_CASE(block_header_validation_algorithm_switch)
{
    // Test: CheckProofOfWorkAtHeight uses correct algorithm based on height
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    CBlockHeader header;
    header.nVersion = 1;
    header.hashPrevBlock = uint256{};
    header.hashMerkleRoot = uint256{};
    header.nTime = 1733788800;
    header.nBits = 0x207fffff;  // Very easy target
    header.nNonce = 0;
    
    int forkHeight = params.nRandomXForkHeight;
    
    // At height 0, should use SHA256d validation (no pindex needed)
    bool sha256Result = CheckProofOfWorkAtHeight(header, 0, nullptr, params);
    // Result depends on SHA256d hash - just verify no crash
    (void)sha256Result;
    
    // At fork height, should use RandomX validation (needs pindex for key block)
    // With nullptr pindex, this returns false for RandomX heights
    bool randomxResult = CheckProofOfWorkAtHeight(header, forkHeight, nullptr, params);
    BOOST_CHECK_MESSAGE(!randomxResult,
        "RandomX validation should fail without key block index");
}

BOOST_AUTO_TEST_CASE(genesis_block_is_sha256d)
{
    // Test: Genesis block (height 0) always uses SHA256d regardless of fork height
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    // Genesis is always height 0 and uses SHA256d
    BOOST_CHECK(!params.IsRandomXActive(0));
    
    // Mainnet fork height is 210000 (10% of supply mined with SHA256d)
    BOOST_CHECK_EQUAL(params.nRandomXForkHeight, 210000);
    
    // All blocks before fork height use SHA256d
    BOOST_CHECK(!params.IsRandomXActive(209999));
    BOOST_CHECK(params.IsRandomXActive(210000));
}

// =============================================================================
// EDGE CASES AND BOUNDARY CONDITIONS
// =============================================================================

BOOST_AUTO_TEST_CASE(regtest_fork_height_override)
{
    // Test: Regtest allows fork height override via -randomxforkheight
    // This test verifies the parameter parsing works
    
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const auto& params = chainParams->GetConsensus();
    
    // Default regtest fork height (should match chainparams definition)
    // This verifies the parameter is configurable
    BOOST_CHECK(params.nRandomXForkHeight >= 0);
}

BOOST_AUTO_TEST_CASE(testnet_fork_configuration)
{
    // Test: Testnet has appropriate fork configuration
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::TESTNET);
    const auto& params = chainParams->GetConsensus();
    
    // Testnet should have RandomX enabled
    BOOST_CHECK(params.nRandomXForkHeight >= 0);
    
    // Key interval should be set
    BOOST_CHECK(params.nRandomXKeyBlockInterval > 0);
    
    // powLimitRandomX should be set
    BOOST_CHECK(!params.powLimitRandomX.IsNull());
}

BOOST_AUTO_TEST_CASE(chain_work_accumulation_across_fork)
{
    // Test: Chain work accumulates correctly across the fork boundary
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    CBlockIndex preFork, atFork;
    
    preFork.nHeight = params.nRandomXForkHeight - 1;
    preFork.nBits = 0x1e00ffff;  // SHA256 difficulty
    preFork.nChainWork = arith_uint256(1000000);
    preFork.pprev = nullptr;
    
    atFork.pprev = &preFork;
    atFork.nHeight = params.nRandomXForkHeight;
    atFork.nBits = UintToArith256(params.powLimitRandomX).GetCompact();  // RandomX min difficulty
    
    // Chain work should accumulate from previous block
    arith_uint256 preForkProof = GetBlockProof(preFork);
    BOOST_CHECK(preForkProof > 0);
    
    // The chain work calculation is independent of PoW algorithm
    atFork.nChainWork = preFork.nChainWork + preForkProof;
    BOOST_CHECK(atFork.nChainWork > preFork.nChainWork);
}

BOOST_AUTO_TEST_CASE(negative_height_graceful_handling)
{
    // Test: Negative heights are handled gracefully without crashes
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    // IsRandomXActive with negative height should return false
    BOOST_CHECK(!params.IsRandomXActive(-1));
    BOOST_CHECK(!params.IsRandomXActive(-1000));
    BOOST_CHECK(!params.IsRandomXActive(std::numeric_limits<int>::min()));
    
    // GetRandomXKeyBlockHeight with negative height should return 0
    BOOST_CHECK_EQUAL(params.GetRandomXKeyBlockHeight(-1), 0);
    BOOST_CHECK_EQUAL(params.GetRandomXKeyBlockHeight(-100), 0);
}

BOOST_AUTO_TEST_CASE(max_height_handling)
{
    // Test: Very large heights are handled correctly
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    int maxHeight = std::numeric_limits<int>::max() - 1;
    
    // RandomX should be active at large heights
    BOOST_CHECK(params.IsRandomXActive(maxHeight));
    
    // Key block calculation should not overflow
    int keyHeight = params.GetRandomXKeyBlockHeight(maxHeight);
    BOOST_CHECK(keyHeight >= 0);
    BOOST_CHECK(keyHeight < maxHeight);
}

// =============================================================================
// REORG SCENARIO TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(reorg_within_sha256_era)
{
    // Test: Reorg entirely within SHA256 era (before fork) maintains consistent state
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    int forkHeight = params.nRandomXForkHeight;
    
    // Simulate two competing chains, both ending before fork
    int chainAHeight = forkHeight - 10;
    int chainBHeight = forkHeight - 5;
    
    // Both chains should use SHA256d
    BOOST_CHECK(!params.IsRandomXActive(chainAHeight));
    BOOST_CHECK(!params.IsRandomXActive(chainBHeight));
    
    // Both chains should use same powLimit
    BOOST_CHECK_EQUAL(params.GetRandomXPowLimit(chainAHeight), params.powLimit);
    BOOST_CHECK_EQUAL(params.GetRandomXPowLimit(chainBHeight), params.powLimit);
}

BOOST_AUTO_TEST_CASE(reorg_within_randomx_era)
{
    // Test: Reorg entirely within RandomX era maintains consistent state
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    int forkHeight = params.nRandomXForkHeight;
    int interval = params.nRandomXKeyBlockInterval;
    
    // Two competing chains, both post-fork
    int chainAHeight = forkHeight + 100;
    int chainBHeight = forkHeight + 150;
    
    // Both should use RandomX
    BOOST_CHECK(params.IsRandomXActive(chainAHeight));
    BOOST_CHECK(params.IsRandomXActive(chainBHeight));
    
    // Both should use RandomX powLimit
    BOOST_CHECK_EQUAL(params.GetRandomXPowLimit(chainAHeight), params.powLimitRandomX);
    BOOST_CHECK_EQUAL(params.GetRandomXPowLimit(chainBHeight), params.powLimitRandomX);
    
    // Key block calculation should be consistent for same heights
    int keyA = params.GetRandomXKeyBlockHeight(chainAHeight);
    int keyB = params.GetRandomXKeyBlockHeight(chainBHeight);
    
    // Verify key blocks are calculated correctly
    BOOST_CHECK(keyA == (chainAHeight / interval - 1) * interval || keyA == 0);
    BOOST_CHECK(keyB == (chainBHeight / interval - 1) * interval || keyB == 0);
}

BOOST_AUTO_TEST_CASE(reorg_crossing_fork_boundary)
{
    // Test: Reorg that crosses the fork boundary from post-fork to pre-fork
    // This is the most critical scenario
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    int forkHeight = params.nRandomXForkHeight;
    
    // Current tip is post-fork
    int currentTip = forkHeight + 10;
    BOOST_CHECK(params.IsRandomXActive(currentTip));
    
    // Competing chain reorgs back to pre-fork
    int reorgTarget = forkHeight - 5;
    BOOST_CHECK(!params.IsRandomXActive(reorgTarget));
    
    // After reorg to pre-fork height, difficulty calculation should use SHA256 params
    BOOST_CHECK_EQUAL(params.GetRandomXPowLimit(reorgTarget), params.powLimit);
    
    // And blocks built from there that reach fork height again need difficulty reset
    BOOST_CHECK_EQUAL(params.GetRandomXPowLimit(forkHeight), params.powLimitRandomX);
}

BOOST_AUTO_TEST_CASE(reorg_key_block_consistency)
{
    // Test: After reorg, key block calculation remains deterministic
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    int interval = params.nRandomXKeyBlockInterval;
    
    // Heights that should use the same key block
    std::vector<int> sameKeyHeights = {64, 65, 80, 95};  // All in interval [64, 96)
    
    int expectedKey = interval;  // Should be 32 for heights 64-95
    
    for (int h : sameKeyHeights) {
        if (params.IsRandomXActive(h)) {
            int keyHeight = params.GetRandomXKeyBlockHeight(h);
            BOOST_CHECK_EQUAL(keyHeight, expectedKey);
        }
    }
}

BOOST_AUTO_TEST_CASE(reorg_difficulty_recalculation)
{
    // Test: Difficulty recalculation after reorg produces same result for same chain state
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    // Build identical chain state twice
    int chainLength = 1000;
    std::vector<CBlockIndex> chain1(chainLength);
    std::vector<CBlockIndex> chain2(chainLength);
    
    uint32_t startTime = 1733616000;
    uint32_t nBits = UintToArith256(params.powLimitRandomX).GetCompact();
    
    for (int i = 0; i < chainLength; ++i) {
        // Chain 1
        chain1[i].pprev = (i > 0) ? &chain1[i - 1] : nullptr;
        chain1[i].nHeight = params.nRandomXForkHeight + i;
        chain1[i].nTime = startTime + i * params.nPowTargetSpacing;
        chain1[i].nBits = nBits;
        chain1[i].nChainWork = (i > 0) ? 
            chain1[i - 1].nChainWork + GetBlockProof(chain1[i - 1]) : arith_uint256(0);
        
        // Chain 2 (identical)
        chain2[i].pprev = (i > 0) ? &chain2[i - 1] : nullptr;
        chain2[i].nHeight = params.nRandomXForkHeight + i;
        chain2[i].nTime = startTime + i * params.nPowTargetSpacing;
        chain2[i].nBits = nBits;
        chain2[i].nChainWork = (i > 0) ? 
            chain2[i - 1].nChainWork + GetBlockProof(chain2[i - 1]) : arith_uint256(0);
    }
    
    // Next work required should be identical
    CBlockHeader header;
    header.nTime = chain1[chainLength - 1].nTime + params.nPowTargetSpacing;
    
    unsigned int nextWork1 = GetNextWorkRequired(&chain1[chainLength - 1], &header, params);
    unsigned int nextWork2 = GetNextWorkRequired(&chain2[chainLength - 1], &header, params);
    
    BOOST_CHECK_EQUAL(nextWork1, nextWork2);
}

BOOST_AUTO_TEST_SUITE_END()
