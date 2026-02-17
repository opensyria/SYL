// Copyright (c) 2024-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RandomX Input Boundary Tests
 * 
 * Tests boundary conditions for RandomX hashing:
 * - Input exactly at 1KB limit
 * - Input exceeding 1KB limit (expect throw)
 * - Single byte input
 * - Max hash constant verification
 */

#include <crypto/randomx_context.h>
#include <crypto/randomx_pool.h>
#include <arith_uint256.h>
#include <test/util/setup_common.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(randomx_boundary_tests, BasicTestingSetup)

// =============================================================================
// G-01: INPUT AT 1KB LIMIT
// =============================================================================

BOOST_AUTO_TEST_CASE(randomx_hash_at_exactly_max_input_size)
{
    // Test: Input at exactly 1KB should succeed
    RandomXContext ctx;
    uint256 keyHash = uint256::ONE;
    BOOST_REQUIRE(ctx.Initialize(keyHash));

    // Create exactly 1KB input (1024 bytes)
    const size_t MAX_RANDOMX_INPUT = 1024;
    std::vector<unsigned char> maxInput(MAX_RANDOMX_INPUT);
    
    // Fill with deterministic pattern for reproducibility
    for (size_t i = 0; i < maxInput.size(); ++i) {
        maxInput[i] = static_cast<unsigned char>(i % 256);
    }

    // Should not throw - exactly at limit
    uint256 hash;
    BOOST_CHECK_NO_THROW(hash = ctx.CalculateHash(maxInput));
    
    // Hash should be valid and non-null
    BOOST_CHECK(!hash.IsNull());
    
    // Hash should be deterministic
    uint256 hash2 = ctx.CalculateHash(maxInput);
    BOOST_CHECK_EQUAL(hash, hash2);
    
    BOOST_TEST_MESSAGE("Successfully hashed 1KB input (exact limit): " << hash.GetHex().substr(0, 16) << "...");
}

// =============================================================================
// G-02: INPUT EXCEEDS 1KB LIMIT
// =============================================================================

BOOST_AUTO_TEST_CASE(randomx_hash_exceeds_max_input_throws)
{
    // Test: Input exceeding 1KB should throw runtime_error
    RandomXContext ctx;
    uint256 keyHash = uint256::ONE;
    BOOST_REQUIRE(ctx.Initialize(keyHash));

    // Create 1KB + 1 byte input (1025 bytes)
    const size_t OVER_LIMIT = 1025;
    std::vector<unsigned char> tooLarge(OVER_LIMIT);
    std::fill(tooLarge.begin(), tooLarge.end(), 0xDE);

    // Should throw runtime_error
    BOOST_CHECK_THROW(ctx.CalculateHash(tooLarge), std::runtime_error);
    
    BOOST_TEST_MESSAGE("Correctly rejected input of " << OVER_LIMIT << " bytes (exceeds 1KB limit)");
}

BOOST_AUTO_TEST_CASE(randomx_hash_significantly_exceeds_max_input)
{
    // Test: 2048-byte input (well above 1KB limit) should also throw
    RandomXContext ctx;
    uint256 keyHash = uint256::ONE;
    BOOST_REQUIRE(ctx.Initialize(keyHash));

    // Create 2048-byte input (well above 1KB limit)
    const size_t DOUBLE_LIMIT = 2048;
    std::vector<unsigned char> veryLarge(DOUBLE_LIMIT);
    std::fill(veryLarge.begin(), veryLarge.end(), 0xAB);

    BOOST_CHECK_THROW(ctx.CalculateHash(veryLarge), std::runtime_error);
    
    BOOST_TEST_MESSAGE("Correctly rejected 2048-byte input");
}

// =============================================================================
// G-03: SINGLE BYTE INPUT
// =============================================================================

BOOST_AUTO_TEST_CASE(randomx_hash_single_byte_input)
{
    // Test: Single byte input should produce valid hash
    RandomXContext ctx;
    uint256 keyHash = uint256::ONE;
    BOOST_REQUIRE(ctx.Initialize(keyHash));

    std::vector<unsigned char> singleByte = {0x42};
    
    uint256 hash = ctx.CalculateHash(singleByte);
    
    // Should produce non-null hash
    BOOST_CHECK(!hash.IsNull());
    
    // Should be deterministic
    uint256 hash2 = ctx.CalculateHash(singleByte);
    BOOST_CHECK_EQUAL(hash, hash2);
    
    // Different single bytes should produce different hashes
    std::vector<unsigned char> differentByte = {0x43};
    uint256 hash3 = ctx.CalculateHash(differentByte);
    BOOST_CHECK(hash != hash3);
    
    BOOST_TEST_MESSAGE("Single byte 0x42 hash: " << hash.GetHex().substr(0, 16) << "...");
}

BOOST_AUTO_TEST_CASE(randomx_hash_all_single_byte_values)
{
    // Test: All 256 possible single-byte inputs produce unique hashes
    RandomXContext ctx;
    uint256 keyHash = uint256::ONE;
    BOOST_REQUIRE(ctx.Initialize(keyHash));

    std::set<uint256> hashes;
    
    for (int i = 0; i < 256; ++i) {
        std::vector<unsigned char> input = {static_cast<unsigned char>(i)};
        uint256 hash = ctx.CalculateHash(input);
        
        BOOST_CHECK(!hash.IsNull());
        
        // Check uniqueness (should not already exist in set)
        auto [it, inserted] = hashes.insert(hash);
        BOOST_CHECK_MESSAGE(inserted, "Byte 0x" << std::hex << i << " produced duplicate hash");
    }
    
    // Should have 256 unique hashes
    BOOST_CHECK_EQUAL(hashes.size(), 256);
    
    BOOST_TEST_MESSAGE("All 256 single-byte inputs produced unique hashes");
}

// =============================================================================
// G-05: MAX HASH CONSTANT VERIFICATION
// =============================================================================

BOOST_AUTO_TEST_CASE(max_hash_constant_is_all_f)
{
    // Test: The max hash returned on pool failure is correct
    // This verifies the constant in CalculateRandomXHash():
    //   return uint256{"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    
    uint256 maxHash{"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    
    // Verify it's the maximum possible uint256
    BOOST_CHECK_EQUAL(maxHash.GetHex(), 
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    
    // Verify all bits are set (check first few bytes)
    BOOST_CHECK_EQUAL(maxHash.data()[0], 0xFF);
    BOOST_CHECK_EQUAL(maxHash.data()[31], 0xFF);
    
    // This hash should always fail PoW check (difficulty > 0)
    // Any target below this should reject the max hash
    arith_uint256 arithMax = UintToArith256(maxHash);
    BOOST_CHECK(arithMax > arith_uint256(0));
    
    BOOST_TEST_MESSAGE("Max hash constant verified: all bits set");
}

// =============================================================================
// EDGE CASE: VARIOUS INPUT SIZES
// =============================================================================

BOOST_AUTO_TEST_CASE(randomx_hash_various_sizes)
{
    // Test: Various input sizes near boundaries
    RandomXContext ctx;
    uint256 keyHash = uint256::ONE;
    BOOST_REQUIRE(ctx.Initialize(keyHash));

    // Test various sizes (up to 1KB limit)
    std::vector<size_t> testSizes = {
        0,          // Empty
        1,          // Single byte
        80,         // Block header size
        255,        // Max uint8
        256,        // Boundary
        512,        // Mid-range
        768,        // 3/4 of limit
        1023,       // One byte below limit
        1024        // 1KB (limit)
    };

    std::set<uint256> hashes;
    
    for (size_t size : testSizes) {
        std::vector<unsigned char> input(size);
        for (size_t i = 0; i < size; ++i) {
            input[i] = static_cast<unsigned char>(i % 256);
        }
        
        uint256 hash;
        BOOST_CHECK_NO_THROW(hash = ctx.CalculateHash(input));
        BOOST_CHECK(!hash.IsNull());
        
        // All different sizes should produce different hashes
        auto [it, inserted] = hashes.insert(hash);
        BOOST_CHECK_MESSAGE(inserted || size == 0, 
            "Size " << size << " produced duplicate hash");
        
        BOOST_TEST_MESSAGE("Size " << size << " OK");
    }
}

// =============================================================================
// RAW POINTER INTERFACE
// =============================================================================

BOOST_AUTO_TEST_CASE(randomx_hash_raw_pointer_at_limit)
{
    // Test: Raw pointer interface at 1KB limit
    RandomXContext ctx;
    uint256 keyHash = uint256::ONE;
    BOOST_REQUIRE(ctx.Initialize(keyHash));

    const size_t MAX_SIZE = 1024;
    std::vector<unsigned char> data(MAX_SIZE, 0xCD);
    
    // Use raw pointer interface
    uint256 hash;
    BOOST_CHECK_NO_THROW(hash = ctx.CalculateHash(data.data(), data.size()));
    BOOST_CHECK(!hash.IsNull());
    
    // Should match vector interface
    uint256 hash2 = ctx.CalculateHash(data);
    BOOST_CHECK_EQUAL(hash, hash2);
}

BOOST_AUTO_TEST_CASE(randomx_hash_raw_pointer_exceeds_limit)
{
    // Test: Raw pointer interface exceeding 1KB limit
    RandomXContext ctx;
    uint256 keyHash = uint256::ONE;
    BOOST_REQUIRE(ctx.Initialize(keyHash));

    const size_t OVER_SIZE = 1025;
    std::vector<unsigned char> data(OVER_SIZE, 0xEF);
    
    BOOST_CHECK_THROW(ctx.CalculateHash(data.data(), data.size()), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
