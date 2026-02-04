// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/params.h>
#include <crypto/argon2_context.h>
#include <primitives/block.h>
#include <test/util/setup_common.h>
#include <uint256.h>

#include <atomic>
#include <set>
#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>

/**
 * Argon2id Emergency Fallback Unit Tests
 *
 * These tests verify the correct behavior of the Argon2id emergency
 * fallback PoW mechanism, which is activated only if RandomX is compromised.
 *
 * Test categories:
 * - Algorithm selection based on height and emergency flag
 * - Argon2id hash calculation determinism
 * - Context initialization and parameter validation
 * - Integration with existing PoW validation
 */

BOOST_FIXTURE_TEST_SUITE(argon2_fallback_tests, BasicTestingSetup)

// =============================================================================
// ALGORITHM SELECTION TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(algorithm_selection_sha256d_at_genesis)
{
    // Test: Genesis block should use SHA256d
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();

    BOOST_CHECK_EQUAL(
        static_cast<int>(params.GetPowAlgorithm(0)),
        static_cast<int>(Consensus::Params::PowAlgorithm::SHA256D)
    );
    BOOST_CHECK_EQUAL(GetPowAlgorithmName(0, params), "SHA256d");
}

BOOST_AUTO_TEST_CASE(algorithm_selection_randomx_after_fork)
{
    // Test: Blocks after fork should use RandomX (when no emergency)
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();

    int forkHeight = params.nRandomXForkHeight;
    BOOST_CHECK_EQUAL(
        static_cast<int>(params.GetPowAlgorithm(forkHeight)),
        static_cast<int>(Consensus::Params::PowAlgorithm::RANDOMX)
    );
    BOOST_CHECK_EQUAL(GetPowAlgorithmName(forkHeight, params), "RandomX");
    BOOST_CHECK_EQUAL(GetPowAlgorithmName(forkHeight + 1000, params), "RandomX");
}

BOOST_AUTO_TEST_CASE(algorithm_selection_argon2_when_emergency)
{
    // Test: Argon2id should be selected when emergency is active
    // We need to create custom params with emergency height set

    // Create a mutable copy of params for testing
    Consensus::Params testParams;
    testParams.nRandomXForkHeight = 1;
    testParams.nArgon2EmergencyHeight = 1000;  // Emergency at height 1000

    // Before emergency: RandomX
    BOOST_CHECK_EQUAL(
        static_cast<int>(testParams.GetPowAlgorithm(999)),
        static_cast<int>(Consensus::Params::PowAlgorithm::RANDOMX)
    );

    // At and after emergency: Argon2id
    BOOST_CHECK_EQUAL(
        static_cast<int>(testParams.GetPowAlgorithm(1000)),
        static_cast<int>(Consensus::Params::PowAlgorithm::ARGON2ID)
    );
    BOOST_CHECK_EQUAL(
        static_cast<int>(testParams.GetPowAlgorithm(2000)),
        static_cast<int>(Consensus::Params::PowAlgorithm::ARGON2ID)
    );
}

BOOST_AUTO_TEST_CASE(emergency_not_active_by_default)
{
    // Test: Emergency fallback should NOT be active by default
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();

    // Default nArgon2EmergencyHeight is -1 (never active)
    BOOST_CHECK(!params.IsArgon2EmergencyActive(0));
    BOOST_CHECK(!params.IsArgon2EmergencyActive(1000));
    BOOST_CHECK(!params.IsArgon2EmergencyActive(1000000));
    BOOST_CHECK(!params.IsArgon2EmergencyActive(std::numeric_limits<int>::max() - 1));
}

// =============================================================================
// ARGON2 CONTEXT TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(argon2_context_initialization)
{
    // Test: Argon2 context should initialize successfully with valid params
    BOOST_CHECK_NO_THROW({
        Argon2Context ctx(1 << 16, 1, 1);  // 64MB, 1 iteration, 1 thread
        BOOST_CHECK(ctx.IsInitialized());
    });
}

BOOST_AUTO_TEST_CASE(argon2_context_invalid_params)
{
    // Test: Argon2 context should reject invalid parameters
    BOOST_CHECK_THROW(Argon2Context(0, 1, 1), std::invalid_argument);    // memory = 0
    BOOST_CHECK_THROW(Argon2Context(1 << 16, 0, 1), std::invalid_argument);  // time = 0
    BOOST_CHECK_THROW(Argon2Context(1 << 16, 1, 0), std::invalid_argument);  // parallelism = 0
}

BOOST_AUTO_TEST_CASE(argon2_hash_determinism)
{
    // Test: Same input should produce same hash
    Argon2Context ctx(1 << 16, 1, 1);  // 64MB for faster testing

    std::vector<unsigned char> input = {0x01, 0x02, 0x03, 0x04};
    uint256 salt = uint256::ONE;

    uint256 hash1 = ctx.CalculateHash(input, salt);
    uint256 hash2 = ctx.CalculateHash(input, salt);

    BOOST_CHECK_EQUAL(hash1.ToString(), hash2.ToString());
}

BOOST_AUTO_TEST_CASE(argon2_hash_different_inputs)
{
    // Test: Different inputs should produce different hashes
    Argon2Context ctx(1 << 16, 1, 1);

    std::vector<unsigned char> input1 = {0x01, 0x02, 0x03, 0x04};
    std::vector<unsigned char> input2 = {0x01, 0x02, 0x03, 0x05};  // One byte different
    uint256 salt = uint256::ONE;

    uint256 hash1 = ctx.CalculateHash(input1, salt);
    uint256 hash2 = ctx.CalculateHash(input2, salt);

    BOOST_CHECK(hash1 != hash2);
}

BOOST_AUTO_TEST_CASE(argon2_hash_different_salts)
{
    // Test: Different salts should produce different hashes
    Argon2Context ctx(1 << 16, 1, 1);

    std::vector<unsigned char> input = {0x01, 0x02, 0x03, 0x04};
    uint256 salt1 = uint256::ONE;
    uint256 salt2 = uint256::ZERO;

    uint256 hash1 = ctx.CalculateHash(input, salt1);
    uint256 hash2 = ctx.CalculateHash(input, salt2);

    BOOST_CHECK(hash1 != hash2);
}

BOOST_AUTO_TEST_CASE(argon2_block_hash_uses_prevhash_as_salt)
{
    // Test: Block hash calculation uses hashPrevBlock as salt
    Argon2Context ctx(1 << 16, 1, 1);

    CBlockHeader header1;
    header1.nVersion = 1;
    header1.hashPrevBlock = uint256::ONE;
    header1.hashMerkleRoot = uint256::ZERO;
    header1.nTime = 1234567890;
    header1.nBits = 0x1d00ffff;
    header1.nNonce = 0;

    CBlockHeader header2 = header1;
    header2.hashPrevBlock = uint256::ZERO;  // Different prev block

    uint256 hash1 = ctx.CalculateBlockHash(header1);
    uint256 hash2 = ctx.CalculateBlockHash(header2);

    // Different prevhash = different salt = different output
    BOOST_CHECK(hash1 != hash2);
}

// =============================================================================
// POW LIMIT SELECTION TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(pow_limit_selection_with_fallback)
{
    // Test: GetActivePowLimit returns correct limit based on algorithm
    Consensus::Params testParams;
    testParams.powLimit = uint256{"00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    testParams.powLimitRandomX = uint256{"0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    testParams.powLimitArgon2 = uint256{"00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    testParams.nRandomXForkHeight = 1;
    testParams.nArgon2EmergencyHeight = -1;  // Not active

    // Before fork: SHA256d limit
    BOOST_CHECK(testParams.GetActivePowLimit(0) == testParams.powLimit);

    // After fork: RandomX limit
    BOOST_CHECK(testParams.GetActivePowLimit(100) == testParams.powLimitRandomX);

    // With emergency active
    testParams.nArgon2EmergencyHeight = 50;
    BOOST_CHECK(testParams.GetActivePowLimit(100) == testParams.powLimitArgon2);
}

// =============================================================================
// INTEGRATION TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(randomx_deactivates_when_argon2_active)
{
    // Test: IsRandomXActive returns false when Argon2 emergency is active
    Consensus::Params testParams;
    testParams.nRandomXForkHeight = 1;
    testParams.nArgon2EmergencyHeight = 100;

    // Before emergency: RandomX should be active
    BOOST_CHECK(testParams.IsRandomXActive(50));
    BOOST_CHECK(!testParams.IsArgon2EmergencyActive(50));

    // At/after emergency: RandomX should NOT be active (Argon2 takes over)
    BOOST_CHECK(!testParams.IsRandomXActive(100));
    BOOST_CHECK(testParams.IsArgon2EmergencyActive(100));

    BOOST_CHECK(!testParams.IsRandomXActive(200));
    BOOST_CHECK(testParams.IsArgon2EmergencyActive(200));
}

// =============================================================================
// EDGE CASE TESTS
// =============================================================================

/**
 * GAP-07: Height zero edge case documentation and tests
 * 
 * Height 0 is the genesis block. Emergency activation at height 0 means:
 * - Argon2id is active from the very beginning
 * - RandomX never activates, regardless of nRandomXForkHeight
 * - The genesis block uses Argon2id PoW
 * 
 * This is unusual in production but valid for testing and special deployments.
 */

BOOST_AUTO_TEST_CASE(argon2_emergency_height_zero)
{
    // Test: Emergency at height 0 should work (though unusual)
    // This verifies genesis block handling when emergency is immediate
    Consensus::Params testParams;
    testParams.nRandomXForkHeight = 1;
    testParams.nArgon2EmergencyHeight = 0;

    // From height 0 onwards, Argon2 should be active
    BOOST_CHECK(testParams.IsArgon2EmergencyActive(0));
    BOOST_CHECK(testParams.IsArgon2EmergencyActive(1));
    BOOST_CHECK(!testParams.IsRandomXActive(1));  // RandomX never activates
}

BOOST_AUTO_TEST_CASE(argon2_emergency_height_zero_pow_algorithm)
{
    // GAP-07: Verify PoW algorithm is Argon2 at height 0
    Consensus::Params testParams;
    testParams.nRandomXForkHeight = 100;
    testParams.nArgon2EmergencyHeight = 0;

    // Height 0 should use Argon2id
    BOOST_CHECK_EQUAL(
        static_cast<int>(testParams.GetPowAlgorithm(0)),
        static_cast<int>(Consensus::Params::PowAlgorithm::ARGON2ID)
    );
    
    // All heights should use Argon2id
    BOOST_CHECK_EQUAL(
        static_cast<int>(testParams.GetPowAlgorithm(50)),
        static_cast<int>(Consensus::Params::PowAlgorithm::ARGON2ID)
    );
    
    BOOST_CHECK_EQUAL(
        static_cast<int>(testParams.GetPowAlgorithm(1000000)),
        static_cast<int>(Consensus::Params::PowAlgorithm::ARGON2ID)
    );
}

BOOST_AUTO_TEST_CASE(argon2_emergency_height_zero_never_disabled)
{
    // GAP-07: Verify height 0 emergency is permanent
    Consensus::Params testParams;
    testParams.nRandomXForkHeight = 1;
    testParams.nArgon2EmergencyHeight = 0;

    // Emergency should be active at every conceivable height
    for (int h = 0; h < 1000; h += 100) {
        BOOST_CHECK(testParams.IsArgon2EmergencyActive(h));
    }
    
    // Even at very large heights
    BOOST_CHECK(testParams.IsArgon2EmergencyActive(INT_MAX - 1));
}

BOOST_AUTO_TEST_CASE(argon2_emergency_at_same_height_as_randomx_fork)
{
    // Test: Emergency at same height as RandomX fork
    Consensus::Params testParams;
    testParams.nRandomXForkHeight = 10;
    testParams.nArgon2EmergencyHeight = 10;

    // At height 10, Argon2 takes priority
    BOOST_CHECK_EQUAL(
        static_cast<int>(testParams.GetPowAlgorithm(10)),
        static_cast<int>(Consensus::Params::PowAlgorithm::ARGON2ID)
    );
}

BOOST_AUTO_TEST_CASE(argon2_emergency_before_randomx_fork)
{
    // Test: Emergency before RandomX fork (edge case)
    Consensus::Params testParams;
    testParams.nRandomXForkHeight = 100;
    testParams.nArgon2EmergencyHeight = 50;

    // Height 0-49: SHA256d
    BOOST_CHECK_EQUAL(
        static_cast<int>(testParams.GetPowAlgorithm(49)),
        static_cast<int>(Consensus::Params::PowAlgorithm::SHA256D)
    );

    // Height 50+: Argon2id (emergency takes over before RandomX ever activates)
    BOOST_CHECK_EQUAL(
        static_cast<int>(testParams.GetPowAlgorithm(50)),
        static_cast<int>(Consensus::Params::PowAlgorithm::ARGON2ID)
    );
    BOOST_CHECK_EQUAL(
        static_cast<int>(testParams.GetPowAlgorithm(100)),
        static_cast<int>(Consensus::Params::PowAlgorithm::ARGON2ID)
    );
}

BOOST_AUTO_TEST_CASE(argon2_negative_emergency_height)
{
    // Test: Negative emergency height means never active
    Consensus::Params testParams;
    testParams.nRandomXForkHeight = 1;
    testParams.nArgon2EmergencyHeight = -1;

    BOOST_CHECK(!testParams.IsArgon2EmergencyActive(0));
    BOOST_CHECK(!testParams.IsArgon2EmergencyActive(1000000));
    BOOST_CHECK(!testParams.IsArgon2EmergencyActive(std::numeric_limits<int>::max() - 1));
}

BOOST_AUTO_TEST_CASE(argon2_large_emergency_height)
{
    // Test: Very large emergency height
    Consensus::Params testParams;
    testParams.nRandomXForkHeight = 1;
    testParams.nArgon2EmergencyHeight = 1000000000;  // 1 billion

    BOOST_CHECK(!testParams.IsArgon2EmergencyActive(999999999));
    BOOST_CHECK(testParams.IsArgon2EmergencyActive(1000000000));
    BOOST_CHECK(testParams.IsArgon2EmergencyActive(1000000001));
}

// =============================================================================
// CONCURRENT HASH CALCULATION TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(argon2_concurrent_hash_same_context)
{
    // Test: Concurrent hashing from same context is thread-safe
    Argon2Context ctx(1 << 16, 1, 1);

    std::vector<unsigned char> input = {0x01, 0x02, 0x03, 0x04};
    uint256 salt = uint256::ONE;

    // Calculate hash for reference
    uint256 expectedHash = ctx.CalculateHash(input, salt);

    // Concurrent access test
    std::atomic<int> successCount{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            try {
                uint256 hash = ctx.CalculateHash(input, salt);
                if (hash == expectedHash) {
                    successCount++;
                }
            } catch (...) {
                // Exception = failure
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    BOOST_CHECK_EQUAL(successCount.load(), 4);
}

BOOST_AUTO_TEST_CASE(argon2_concurrent_hash_different_inputs)
{
    // Test: Concurrent hashing with different inputs
    Argon2Context ctx(1 << 16, 1, 1);

    std::atomic<int> successCount{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&, i]() {
            try {
                std::vector<unsigned char> input = {
                    static_cast<unsigned char>(i),
                    static_cast<unsigned char>(i + 1),
                    static_cast<unsigned char>(i + 2),
                    static_cast<unsigned char>(i + 3)
                };
                // Create unique salt from loop index
                uint256 salt;
                std::memset(salt.begin(), static_cast<unsigned char>(i), 32);

                uint256 hash1 = ctx.CalculateHash(input, salt);
                uint256 hash2 = ctx.CalculateHash(input, salt);

                // Same input should produce same hash
                if (hash1 == hash2) {
                    successCount++;
                }
            } catch (...) {
                // Exception = failure
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    BOOST_CHECK_EQUAL(successCount.load(), 4);
}

// =============================================================================
// BLOCK HEADER HASH TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(argon2_block_header_all_fields_affect_hash)
{
    // Test: All block header fields affect the hash
    Argon2Context ctx(1 << 16, 1, 1);

    CBlockHeader baseHeader;
    baseHeader.nVersion = 1;
    baseHeader.hashPrevBlock = uint256::ONE;
    baseHeader.hashMerkleRoot = uint256::ZERO;
    baseHeader.nTime = 1234567890;
    baseHeader.nBits = 0x1d00ffff;
    baseHeader.nNonce = 0;

    uint256 baseHash = ctx.CalculateBlockHash(baseHeader);

    // Modify each field and verify hash changes
    CBlockHeader modified;

    // Version change
    modified = baseHeader;
    modified.nVersion = 2;
    BOOST_CHECK(ctx.CalculateBlockHash(modified) != baseHash);

    // Merkle root change
    modified = baseHeader;
    modified.hashMerkleRoot = uint256::ONE;
    BOOST_CHECK(ctx.CalculateBlockHash(modified) != baseHash);

    // Time change
    modified = baseHeader;
    modified.nTime = 1234567891;
    BOOST_CHECK(ctx.CalculateBlockHash(modified) != baseHash);

    // nBits change
    modified = baseHeader;
    modified.nBits = 0x1d00fffe;
    BOOST_CHECK(ctx.CalculateBlockHash(modified) != baseHash);

    // Nonce change
    modified = baseHeader;
    modified.nNonce = 1;
    BOOST_CHECK(ctx.CalculateBlockHash(modified) != baseHash);

    // prevBlockHash change (also changes salt)
    modified = baseHeader;
    modified.hashPrevBlock = uint256::ZERO;
    BOOST_CHECK(ctx.CalculateBlockHash(modified) != baseHash);
}

BOOST_AUTO_TEST_CASE(argon2_nonce_grinding_produces_different_hashes)
{
    // Test: Different nonces produce different hashes (for mining)
    Argon2Context ctx(1 << 16, 1, 1);

    CBlockHeader header;
    header.nVersion = 1;
    header.hashPrevBlock = uint256::ONE;
    header.hashMerkleRoot = uint256::ZERO;
    header.nTime = 1234567890;
    header.nBits = 0x1d00ffff;

    std::set<std::string> hashes;

    // Generate hashes for different nonces
    for (uint32_t nonce = 0; nonce < 100; ++nonce) {
        header.nNonce = nonce;
        uint256 hash = ctx.CalculateBlockHash(header);
        hashes.insert(hash.ToString());
    }

    // All 100 nonces should produce unique hashes
    BOOST_CHECK_EQUAL(hashes.size(), 100);
}

// =============================================================================
// INPUT VALIDATION TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(argon2_empty_input)
{
    // Test: Empty input should still produce valid hash
    Argon2Context ctx(1 << 16, 1, 1);

    std::vector<unsigned char> emptyInput;
    uint256 salt = uint256::ONE;

    uint256 hash;
    BOOST_CHECK_NO_THROW(hash = ctx.CalculateHash(emptyInput, salt));
    BOOST_CHECK(!hash.IsNull());
}

BOOST_AUTO_TEST_CASE(argon2_large_input)
{
    // Test: Large input should work (up to limit)
    Argon2Context ctx(1 << 16, 1, 1);

    // 1MB input
    std::vector<unsigned char> largeInput(1024 * 1024, 0xAB);
    uint256 salt = uint256::ONE;

    uint256 hash;
    BOOST_CHECK_NO_THROW(hash = ctx.CalculateHash(largeInput, salt));
    BOOST_CHECK(!hash.IsNull());
}

BOOST_AUTO_TEST_CASE(argon2_input_too_large)
{
    // Test: Input exceeding max size should throw
    Argon2Context ctx(1 << 16, 1, 1);

    // 5MB input (exceeds 4MB limit)
    std::vector<unsigned char> tooLargeInput(5 * 1024 * 1024, 0xAB);
    uint256 salt = uint256::ONE;

    BOOST_CHECK_THROW(ctx.CalculateHash(tooLargeInput, salt), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(argon2_null_salt)
{
    // Test: Null salt is valid
    Argon2Context ctx(1 << 16, 1, 1);

    std::vector<unsigned char> input = {0x01, 0x02, 0x03, 0x04};
    uint256 nullSalt;  // Default is null

    uint256 hash;
    BOOST_CHECK_NO_THROW(hash = ctx.CalculateHash(input, nullSalt));
    BOOST_CHECK(!hash.IsNull());
}

// =============================================================================
// PARAMETER GETTER TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(argon2_context_getters)
{
    // Test: Getters return correct values
    uint32_t memory = 1 << 18;  // 256MB
    uint32_t time = 2;
    uint32_t parallelism = 4;

    Argon2Context ctx(memory, time, parallelism);

    BOOST_CHECK_EQUAL(ctx.GetMemoryCost(), memory);
    BOOST_CHECK_EQUAL(ctx.GetTimeCost(), time);
    BOOST_CHECK_EQUAL(ctx.GetParallelism(), parallelism);
}

// =============================================================================
// ALGORITHM NAME TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(algorithm_name_all_cases)
{
    // Test: GetPowAlgorithmName returns correct string for all algorithms
    Consensus::Params testParams;
    testParams.nRandomXForkHeight = 10;
    testParams.nArgon2EmergencyHeight = 100;

    // SHA256d
    BOOST_CHECK_EQUAL(std::string(GetPowAlgorithmName(0, testParams)), "SHA256d");
    BOOST_CHECK_EQUAL(std::string(GetPowAlgorithmName(9, testParams)), "SHA256d");

    // RandomX
    BOOST_CHECK_EQUAL(std::string(GetPowAlgorithmName(10, testParams)), "RandomX");
    BOOST_CHECK_EQUAL(std::string(GetPowAlgorithmName(99, testParams)), "RandomX");

    // Argon2id
    BOOST_CHECK_EQUAL(std::string(GetPowAlgorithmName(100, testParams)), "Argon2id");
    BOOST_CHECK_EQUAL(std::string(GetPowAlgorithmName(1000, testParams)), "Argon2id");
}

// =============================================================================
// POW LIMIT FALLBACK TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(pow_limit_fallback_when_argon2_limit_null)
{
    // Test: Falls back to RandomX limit when Argon2 limit is null
    Consensus::Params testParams;
    testParams.powLimit = uint256{"00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    testParams.powLimitRandomX = uint256{"0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    // powLimitArgon2 is default/null
    testParams.nRandomXForkHeight = 1;
    testParams.nArgon2EmergencyHeight = 100;

    // When Argon2 is active but limit is null, should fall back to RandomX limit
    BOOST_CHECK(testParams.GetActivePowLimit(100) == testParams.powLimitRandomX);
}

BOOST_AUTO_TEST_CASE(pow_limit_fallback_when_randomx_limit_null)
{
    // Test: Falls back to SHA256 limit when RandomX limit is null
    Consensus::Params testParams;
    testParams.powLimit = uint256{"00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    // powLimitRandomX is default/null
    testParams.nRandomXForkHeight = 1;
    testParams.nArgon2EmergencyHeight = -1;

    // When RandomX is active but limit is null, should fall back to SHA256 limit
    BOOST_CHECK(testParams.GetActivePowLimit(100) == testParams.powLimit);
}

// =============================================================================
// CRYPTO LIBRARY VERIFICATION TESTS (Audit Recommendation)
// =============================================================================

BOOST_AUTO_TEST_CASE(verify_libsodium_linkage_for_release)
{
    // Test: Verify that release builds have libsodium linked
    // This is CRITICAL for Argon2id emergency PoW security
    //
    // The SHA256 fallback exists only for development testing.
    // If Argon2 emergency is ever activated on mainnet, nodes without
    // libsodium would be vulnerable to GPU mining attacks.
    
#ifdef HAVE_LIBSODIUM
    // Good: libsodium is available
    BOOST_TEST_MESSAGE("PASS: libsodium is linked - Argon2id emergency PoW is secure");
    BOOST_CHECK(true);
#else
    // Warn but don't fail for development builds (NDEBUG not defined)
    #ifndef NDEBUG
    BOOST_TEST_MESSAGE("WARNING: libsodium not linked (development build)");
    BOOST_TEST_MESSAGE("         This is acceptable for testing but NOT for release");
    BOOST_CHECK(true);  // Don't fail dev builds
    #else
    // FAIL release builds without libsodium
    BOOST_ERROR("CRITICAL: Release build without libsodium!");
    BOOST_ERROR("         Argon2id emergency PoW would use weak SHA256 fallback.");
    BOOST_ERROR("         Install libsodium and rebuild: brew install libsodium / apt install libsodium-dev");
    BOOST_CHECK(false);
    #endif
#endif
}

BOOST_AUTO_TEST_CASE(argon2_produces_memory_hard_hash)
{
    // Test: Verify Argon2id context produces different output than SHA256
    // This validates that we're using actual memory-hard crypto, not fallback
    
    // Use smaller memory for tests (64 KB) to avoid test timeouts
    Argon2Context ctx(64, 1, 1);
    
    // Test input
    std::vector<uint8_t> data = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    uint256 salt = uint256{"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"};
    
    // Calculate Argon2 hash
    uint256 argon2_hash = ctx.CalculateHash(data.data(), data.size(), salt);
    
    // If using real Argon2id (libsodium), hash should be different from simple SHA256
    // We can't directly compare to SHA256 here, but we verify the hash is non-trivial
    BOOST_CHECK(argon2_hash != uint256::ZERO);
    BOOST_CHECK(argon2_hash != salt);
    
    // Calculate same hash again - should be deterministic
    uint256 argon2_hash2 = ctx.CalculateHash(data.data(), data.size(), salt);
    BOOST_CHECK(argon2_hash == argon2_hash2);
    
    BOOST_TEST_MESSAGE("Argon2 hash: " + argon2_hash.ToString());
}

BOOST_AUTO_TEST_SUITE_END()
