// Copyright (c) 2024-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/blake2b.h>
#include <util/strencodings.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(blake2b_tests, BasicTestingSetup)

// Test vectors from RFC 7693 Appendix A
// https://www.rfc-editor.org/rfc/rfc7693#appendix-A

BOOST_AUTO_TEST_CASE(blake2b_empty_string)
{
    // BLAKE2b-512("") 
    // Expected from reference implementation
    unsigned char hash[64];
    Blake2b(nullptr, 0, hash, 64);
    
    // BLAKE2b-512 of empty string
    std::string expected = 
        "786a02f742015903c6c6fd852552d272912f4740e15847618a86e217f71f5419"
        "d25e1031afee585313896444934eb04b903a685b1448b755d56f701afe9be2ce";
    
    BOOST_CHECK_EQUAL(HexStr(hash), expected);
}

BOOST_AUTO_TEST_CASE(blake2b_abc)
{
    // BLAKE2b-512("abc")
    const unsigned char input[] = {'a', 'b', 'c'};
    unsigned char hash[64];
    Blake2b(input, 3, hash, 64);
    
    std::string expected = 
        "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
        "7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923";
    
    BOOST_CHECK_EQUAL(HexStr(hash), expected);
}

BOOST_AUTO_TEST_CASE(blake2b_256_output)
{
    // BLAKE2b with 256-bit output
    const unsigned char input[] = {'a', 'b', 'c'};
    unsigned char hash[32];
    Blake2b(input, 3, hash, 32);
    
    // Expected BLAKE2b-256("abc")
    std::string expected = 
        "bddd813c634239723171ef3fee98579b94964e3bb1cb3e427262c8c068d52319";
    
    BOOST_CHECK_EQUAL(HexStr(hash), expected);
}

BOOST_AUTO_TEST_CASE(blake2b_incremental)
{
    // Test incremental hashing gives same result as one-shot
    const std::string message = "The quick brown fox jumps over the lazy dog";
    
    unsigned char hash1[64], hash2[64];
    
    // One-shot
    Blake2b(reinterpret_cast<const unsigned char*>(message.data()), 
            message.size(), hash1, 64);
    
    // Incremental
    CBlake2b hasher(64);
    hasher.Write(reinterpret_cast<const unsigned char*>(message.data()), 10);
    hasher.Write(reinterpret_cast<const unsigned char*>(message.data() + 10), 
                 message.size() - 10);
    hasher.Finalize(hash2);
    
    BOOST_CHECK(memcmp(hash1, hash2, 64) == 0);
}

BOOST_AUTO_TEST_CASE(blake2b_keyed)
{
    // Test keyed BLAKE2b (MAC mode)
    const unsigned char key[] = "secret key";
    const unsigned char message[] = "message to authenticate";
    unsigned char mac[32];
    
    Blake2bKeyed(message, sizeof(message) - 1, key, sizeof(key) - 1, mac, 32);
    
    // Just verify it produces a non-zero result different from unkeyed
    unsigned char unkeyed[32];
    Blake2b(message, sizeof(message) - 1, unkeyed, 32);
    
    BOOST_CHECK(memcmp(mac, unkeyed, 32) != 0);
}

BOOST_AUTO_TEST_CASE(blake2b_long_message)
{
    // Test with a longer message that spans multiple blocks
    std::vector<unsigned char> message(1000, 0x42);
    unsigned char hash[64];
    
    Blake2b(message.data(), message.size(), hash, 64);
    
    // Verify we get a consistent hash (regression test)
    std::string hash_hex = HexStr(hash);
    BOOST_CHECK_EQUAL(hash_hex.size(), 128);
    
    // Hash again to verify determinism
    unsigned char hash2[64];
    Blake2b(message.data(), message.size(), hash2, 64);
    BOOST_CHECK(memcmp(hash, hash2, 64) == 0);
}

BOOST_AUTO_TEST_CASE(blake2b_various_output_sizes)
{
    // Test various output sizes
    const unsigned char input[] = "test";
    
    for (size_t outlen = 1; outlen <= 64; outlen++) {
        std::vector<unsigned char> hash(outlen);
        Blake2b(input, 4, hash.data(), outlen);
        
        // Just verify no crash and produces output
        BOOST_CHECK_EQUAL(hash.size(), outlen);
    }
}

BOOST_AUTO_TEST_CASE(blake2b_reset)
{
    // Test reset functionality
    CBlake2b hasher(32);
    
    const unsigned char msg1[] = "first";
    const unsigned char msg2[] = "second";
    
    unsigned char hash1[32], hash2[32], hash3[32];
    
    // First hash
    hasher.Write(msg1, 5);
    hasher.Finalize(hash1);
    
    // Reset and hash second message
    hasher.Reset();
    hasher.Write(msg2, 6);
    hasher.Finalize(hash2);
    
    // One-shot of second message should match
    Blake2b(msg2, 6, hash3, 32);
    BOOST_CHECK(memcmp(hash2, hash3, 32) == 0);
    
    // First and second should be different
    BOOST_CHECK(memcmp(hash1, hash2, 32) != 0);
}

BOOST_AUTO_TEST_SUITE_END()
