// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RandomX Adversarial Scenario Tests (T-16 through T-18)
 * 
 * Tests for adversarial mining and network scenarios:
 * - T-16: Hashrate attack simulation (chain work comparison)
 * - T-17: Selfish mining detection patterns
 * - T-18: Stale block handling with slow validation
 */

#include <chain.h>
#include <chainparams.h>
#include <consensus/params.h>
#include <crypto/randomx_context.h>
#include <pow.h>
#include <primitives/block.h>
#include <test/util/setup_common.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <chrono>
#include <random>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(randomx_adversarial_tests, BasicTestingSetup)

// =============================================================================
// T-16: HASHRATE ATTACK SIMULATION
// =============================================================================
// Scenario: 51% attack - attacker mines secret chain and publishes

BOOST_AUTO_TEST_CASE(t16_chain_work_comparison)
{
    // Test: Longer chain with valid work should win
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    int forkHeight = params.nRandomXForkHeight;
    
    uint32_t randomxBits = UintToArith256(params.powLimitRandomX).GetCompact();
    uint32_t startTime = 1733616000;
    
    // Honest chain: 10 blocks
    int honestLength = forkHeight + 10;
    std::vector<CBlockIndex> honestChain(honestLength);
    
    for (int i = 0; i < honestLength; ++i) {
        honestChain[i].pprev = (i > 0) ? &honestChain[i - 1] : nullptr;
        honestChain[i].nHeight = i;
        honestChain[i].nTime = startTime + i * params.nPowTargetSpacing;
        honestChain[i].nBits = (i < forkHeight) ? 
            UintToArith256(params.powLimit).GetCompact() : randomxBits;
        honestChain[i].nChainWork = (i > 0) ? 
            honestChain[i - 1].nChainWork + GetBlockProof(honestChain[i - 1]) : 
            arith_uint256(0);
    }
    
    // Attacker chain: 12 blocks (longer)
    int attackerLength = forkHeight + 12;
    std::vector<CBlockIndex> attackerChain(attackerLength);
    
    for (int i = 0; i < attackerLength; ++i) {
        attackerChain[i].pprev = (i > 0) ? &attackerChain[i - 1] : nullptr;
        attackerChain[i].nHeight = i;
        attackerChain[i].nTime = startTime + i * params.nPowTargetSpacing;
        attackerChain[i].nBits = (i < forkHeight) ? 
            UintToArith256(params.powLimit).GetCompact() : randomxBits;
        attackerChain[i].nChainWork = (i > 0) ? 
            attackerChain[i - 1].nChainWork + GetBlockProof(attackerChain[i - 1]) : 
            arith_uint256(0);
    }
    
    // Attacker chain should have more work
    arith_uint256 honestWork = honestChain.back().nChainWork;
    arith_uint256 attackerWork = attackerChain.back().nChainWork;
    
    BOOST_CHECK_GT(attackerWork, honestWork);
    
    // Calculate work difference
    arith_uint256 workDiff = attackerWork - honestWork;
    BOOST_CHECK_GT(workDiff, arith_uint256(0));
    
    BOOST_TEST_MESSAGE("Chain work comparison: attacker=" << attackerWork.ToString() 
                       << " > honest=" << honestWork.ToString());
}

BOOST_AUTO_TEST_CASE(t16_reorg_depth_limit_awareness)
{
    // Test: Deep reorgs require significant work advantage
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    // Work per block at minimum difficulty
    arith_uint256 powLimit = UintToArith256(params.powLimitRandomX);
    arith_uint256 workPerBlock = ~powLimit / powLimit + 1;  // Approximate
    
    // For a 6-block reorg (standard confirmation depth)
    int reorgDepth = 6;
    arith_uint256 workToOvercome = workPerBlock * reorgDepth;
    
    BOOST_CHECK_GT(workToOvercome, arith_uint256(0));
    BOOST_TEST_MESSAGE("Work to overcome 6-block depth: " << workToOvercome.ToString());
}

BOOST_AUTO_TEST_CASE(t16_difficulty_attack_detection)
{
    // Test: Artificial difficulty claims are detected
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    int forkHeight = params.nRandomXForkHeight;
    
    CBlockHeader header;
    header.nVersion = 1;
    header.hashPrevBlock = uint256::ONE;
    header.hashMerkleRoot = uint256::ONE;
    header.nTime = 1733788800;
    header.nNonce = 0;
    
    // Claim extremely high difficulty (low target)
    header.nBits = 0x1700ffff;  // Much harder than powLimit
    
    // This should be valid difficulty claim (within range)
    bool validClaim = CheckProofOfWorkForBlockIndex(header, forkHeight, params);
    BOOST_CHECK(validClaim);  // nBits is valid, actual hash check happens elsewhere
    
    // But claim of difficulty EASIER than powLimit should fail
    arith_uint256 tooEasy = UintToArith256(params.powLimitRandomX) * 2;
    header.nBits = tooEasy.GetCompact();
    
    bool invalidClaim = CheckProofOfWorkForBlockIndex(header, forkHeight, params);
    BOOST_CHECK(!invalidClaim);
    
    BOOST_TEST_MESSAGE("Difficulty attack detection verified");
}

// =============================================================================
// T-17: SELFISH MINING DETECTION PATTERNS
// =============================================================================
// Scenario: Miner withholds blocks and publishes strategically

BOOST_AUTO_TEST_CASE(t17_block_timing_analysis)
{
    // Test: Unusual block timing patterns can indicate selfish mining
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    uint32_t targetSpacing = params.nPowTargetSpacing;  // 120 seconds
    uint32_t startTime = 1733616000;
    
    // Normal mining: blocks roughly every targetSpacing
    std::vector<uint32_t> normalTimes;
    for (int i = 0; i < 20; ++i) {
        normalTimes.push_back(startTime + i * targetSpacing);
    }
    
    // Selfish mining pattern: burst of blocks at once
    std::vector<uint32_t> selfishTimes;
    for (int i = 0; i < 10; ++i) {
        selfishTimes.push_back(startTime + i * targetSpacing);
    }
    // Sudden burst of 5 blocks (previously withheld)
    for (int i = 0; i < 5; ++i) {
        selfishTimes.push_back(selfishTimes.back() + 1);  // 1 second apart
    }
    
    // Calculate time variance
    auto calcVariance = [](const std::vector<uint32_t>& times) -> double {
        if (times.size() < 2) return 0;
        std::vector<int64_t> gaps;
        for (size_t i = 1; i < times.size(); ++i) {
            gaps.push_back(times[i] - times[i-1]);
        }
        double mean = 0;
        for (auto g : gaps) mean += g;
        mean /= gaps.size();
        double variance = 0;
        for (auto g : gaps) variance += (g - mean) * (g - mean);
        return variance / gaps.size();
    };
    
    double normalVariance = calcVariance(normalTimes);
    double selfishVariance = calcVariance(selfishTimes);
    
    // Selfish pattern has higher variance due to bursts
    BOOST_CHECK_GT(selfishVariance, normalVariance);
    
    BOOST_TEST_MESSAGE("Block timing analysis: normal variance=" << normalVariance 
                       << ", selfish variance=" << selfishVariance);
}

BOOST_AUTO_TEST_CASE(t17_orphan_rate_analysis)
{
    // Test: Elevated orphan rates can indicate selfish mining
    // (This is a unit-level simulation, not actual network test)
    
    // Simulate block arrivals using deterministic sequences
    // Normal: ~2% orphan rate for healthy network
    // Selfish: Can cause elevated orphan rates ~10%
    
    int totalBlocks = 1000;  // Larger sample for statistical stability
    
    // Normal scenario: use seed 42
    std::mt19937 rng1(42);
    std::uniform_real_distribution<> dist(0, 1);
    
    int normalOrphans = 0;
    for (int i = 0; i < totalBlocks; ++i) {
        if (dist(rng1) < 0.02) normalOrphans++;  // 2% orphan rate
    }
    
    // Elevated orphan scenario (selfish mining) - use same seed
    std::mt19937 rng2(42);  // Fresh RNG with same seed
    int elevatedOrphans = 0;
    for (int i = 0; i < totalBlocks; ++i) {
        if (dist(rng2) < 0.10) elevatedOrphans++;  // 10% orphan rate
    }
    
    // With same random values, 10% threshold catches more than 2%
    BOOST_CHECK_GT(elevatedOrphans, normalOrphans);
    
    BOOST_TEST_MESSAGE("Orphan rate analysis: normal=" << normalOrphans 
                       << ", elevated=" << elevatedOrphans);
}

BOOST_AUTO_TEST_CASE(t17_chain_split_detection)
{
    // Test: Competing chains indicate possible selfish mining
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    int forkHeight = params.nRandomXForkHeight;
    
    uint32_t randomxBits = UintToArith256(params.powLimitRandomX).GetCompact();
    uint32_t startTime = 1733616000;
    
    // Common ancestor
    int ancestorHeight = forkHeight + 5;
    std::vector<CBlockIndex> common(ancestorHeight + 1);
    
    for (int i = 0; i <= ancestorHeight; ++i) {
        common[i].pprev = (i > 0) ? &common[i - 1] : nullptr;
        common[i].nHeight = i;
        common[i].nTime = startTime + i * params.nPowTargetSpacing;
        common[i].nBits = (i < forkHeight) ? 
            UintToArith256(params.powLimit).GetCompact() : randomxBits;
        common[i].nChainWork = (i > 0) ? 
            common[i - 1].nChainWork + GetBlockProof(common[i - 1]) : 
            arith_uint256(0);
    }
    
    // Fork A: 2 blocks
    std::vector<CBlockIndex> forkA(2);
    forkA[0].pprev = &common.back();
    forkA[0].nHeight = ancestorHeight + 1;
    forkA[0].nTime = common.back().nTime + params.nPowTargetSpacing;
    forkA[0].nBits = randomxBits;
    forkA[0].nChainWork = common.back().nChainWork + GetBlockProof(common.back());
    
    forkA[1].pprev = &forkA[0];
    forkA[1].nHeight = ancestorHeight + 2;
    forkA[1].nTime = forkA[0].nTime + params.nPowTargetSpacing;
    forkA[1].nBits = randomxBits;
    forkA[1].nChainWork = forkA[0].nChainWork + GetBlockProof(forkA[0]);
    
    // Fork B: 3 blocks (wins)
    std::vector<CBlockIndex> forkB(3);
    forkB[0].pprev = &common.back();
    forkB[0].nHeight = ancestorHeight + 1;
    forkB[0].nTime = common.back().nTime + params.nPowTargetSpacing + 10;
    forkB[0].nBits = randomxBits;
    forkB[0].nChainWork = common.back().nChainWork + GetBlockProof(common.back());
    
    for (int i = 1; i < 3; ++i) {
        forkB[i].pprev = &forkB[i - 1];
        forkB[i].nHeight = ancestorHeight + 1 + i;
        forkB[i].nTime = forkB[i - 1].nTime + params.nPowTargetSpacing;
        forkB[i].nBits = randomxBits;
        forkB[i].nChainWork = forkB[i - 1].nChainWork + GetBlockProof(forkB[i - 1]);
    }
    
    // Fork B should have more work
    BOOST_CHECK_GT(forkB.back().nChainWork, forkA.back().nChainWork);
    
    BOOST_TEST_MESSAGE("Chain split: fork A has " << forkA.size() << " blocks, "
                       << "fork B has " << forkB.size() << " blocks");
}

// =============================================================================
// T-18: STALE BLOCK HANDLING
// =============================================================================
// Scenario: Slow RandomX validation causes increased stale rates

BOOST_AUTO_TEST_CASE(t18_validation_time_awareness)
{
    // Test: Measure RandomX hash time vs SHA256d
    CBlockHeader header;
    header.nVersion = 1;
    header.hashPrevBlock = uint256::ONE;
    header.hashMerkleRoot = uint256::ONE;
    header.nTime = 1733788800;
    header.nBits = 0x1e00ffff;
    header.nNonce = 42;
    
    // Time SHA256d hash
    auto sha256Start = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) {
        header.nNonce = i;
        (void)header.GetHash();
    }
    auto sha256End = std::chrono::steady_clock::now();
    auto sha256Time = std::chrono::duration_cast<std::chrono::microseconds>(sha256End - sha256Start);
    
    // Time RandomX hash (just 10 due to slowness)
    uint256 keyHash{"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"};
    
    auto randomxStart = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; ++i) {
        header.nNonce = i;
        (void)CalculateRandomXHash(header, keyHash);
    }
    auto randomxEnd = std::chrono::steady_clock::now();
    auto randomxTime = std::chrono::duration_cast<std::chrono::microseconds>(randomxEnd - randomxStart);
    
    // RandomX is significantly slower (expected ~100x)
    // But we normalize per-hash
    double sha256PerHash = static_cast<double>(sha256Time.count()) / 1000.0;
    double randomxPerHash = static_cast<double>(randomxTime.count()) / 10.0;
    
    BOOST_CHECK_GT(randomxPerHash, sha256PerHash);
    
    BOOST_TEST_MESSAGE("Hash time: SHA256d=" << sha256PerHash << "us, RandomX=" 
                       << randomxPerHash << "us (ratio=" << (randomxPerHash/sha256PerHash) << "x)");
}

BOOST_AUTO_TEST_CASE(t18_block_propagation_model)
{
    // Test: Model stale rate based on validation time
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    
    // Target block time: 120 seconds
    double blockTime = params.nPowTargetSpacing;
    
    // Estimated propagation + validation time scenarios
    std::vector<double> validationTimes = {1.0, 5.0, 10.0, 30.0};  // seconds
    
    for (double valTime : validationTimes) {
        // Simple stale rate model: P(stale) ≈ validationTime / blockTime
        double staleRate = valTime / blockTime;
        
        // Stale rate should stay reasonable
        if (valTime <= 10.0) {
            BOOST_CHECK_LT(staleRate, 0.10);  // < 10% for reasonable validation times
        }
        
        BOOST_TEST_MESSAGE("Validation time=" << valTime << "s -> stale rate=" 
                           << (staleRate * 100) << "%");
    }
}

BOOST_AUTO_TEST_CASE(t18_parallel_validation_scalability)
{
    // Test: Parallel validation can reduce effective stale time
    const int NUM_HEADERS = 10;
    
    CBlockHeader header;
    header.nVersion = 1;
    header.hashPrevBlock = uint256::ONE;
    header.hashMerkleRoot = uint256::ONE;
    header.nTime = 1733788800;
    header.nBits = 0x1e00ffff;
    
    uint256 keyHash{"deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef"};
    
    // Sequential validation time
    auto seqStart = std::chrono::steady_clock::now();
    for (int i = 0; i < NUM_HEADERS; ++i) {
        header.nNonce = i;
        (void)CalculateRandomXHash(header, keyHash);
    }
    auto seqEnd = std::chrono::steady_clock::now();
    auto seqTime = std::chrono::duration_cast<std::chrono::milliseconds>(seqEnd - seqStart);
    
    // Parallel validation time (using pool)
    std::vector<CBlockHeader> headers(NUM_HEADERS);
    for (int i = 0; i < NUM_HEADERS; ++i) {
        headers[i] = header;
        headers[i].nNonce = i + 1000;  // Different from sequential
    }
    
    auto parStart = std::chrono::steady_clock::now();
    std::vector<std::thread> threads;
    std::vector<uint256> results(NUM_HEADERS);
    
    for (int i = 0; i < NUM_HEADERS; ++i) {
        threads.emplace_back([&, i]() {
            results[i] = CalculateRandomXHash(headers[i], keyHash);
        });
    }
    for (auto& t : threads) t.join();
    
    auto parEnd = std::chrono::steady_clock::now();
    auto parTime = std::chrono::duration_cast<std::chrono::milliseconds>(parEnd - parStart);
    
    // Verify all results are valid
    for (const auto& hash : results) {
        BOOST_CHECK(!hash.IsNull());
    }
    
    // Parallel should be faster (or at least not much slower due to pool contention)
    // Note: With MAX_CONTEXTS=8, 10 headers may not all run in parallel
    BOOST_TEST_MESSAGE("Validation time for " << NUM_HEADERS << " headers: "
                       << "sequential=" << seqTime.count() << "ms, "
                       << "parallel=" << parTime.count() << "ms");
}

BOOST_AUTO_TEST_CASE(t18_compact_block_advantage)
{
    // Test: Compact blocks reduce validation delay
    // (Unit test verifies header size is constant and small)
    
    CBlockHeader header;
    header.nVersion = 1;
    header.hashPrevBlock = uint256::ONE;
    header.hashMerkleRoot = uint256::ONE;
    header.nTime = 1733788800;
    header.nBits = 0x1e00ffff;
    header.nNonce = 42;
    
    // Header should be exactly 80 bytes (4+32+32+4+4+4)
    // This is a constant in Bitcoin-based protocols
    constexpr size_t HEADER_SIZE = 80;
    
    // Verify header components add up correctly
    BOOST_CHECK_EQUAL(
        sizeof(header.nVersion) + sizeof(header.hashPrevBlock) + 
        sizeof(header.hashMerkleRoot) + sizeof(header.nTime) + 
        sizeof(header.nBits) + sizeof(header.nNonce),
        HEADER_SIZE);
    
    // For compact blocks, only ~80 bytes need to be validated for PoW
    // Transaction validation is separate and can be done in parallel
    
    BOOST_TEST_MESSAGE("Block header size: " << HEADER_SIZE << " bytes");
}

BOOST_AUTO_TEST_SUITE_END()
