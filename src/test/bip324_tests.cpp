// Copyright (c) 2023 The Bitcoin Core developers
// Copyright (c) 2024 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bip324.h>
#include <chainparams.h>
#include <key.h>
#include <pubkey.h>
#include <span.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(bip324_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(packet_test_vectors) {
    // BIP324 key derivation uses network magic in the HKDF process. We use mainnet params here
    // as that is what the test vectors are written for.
    SelectParams(ChainType::MAIN);

    // OpenSY BIP324 test vector
    // Generated using OpenSY's HKDF salt: "opensy_v2_shared_secret" + 0x53594c4d
    // Input keys are from Bitcoin's BIP324 test vectors (first vector)
    // Output values (session_id, garbage terminators, ciphertext) are OpenSY-specific
    
    // Input parameters
    const uint32_t in_idx = 1;
    const auto in_priv_ours = ParseHex("61062ea5071d800bbfd59e2e8b53d47d194b095ae5a4df04936b49772ef0d4d7");
    const auto in_ellswift_ours = ParseHex<std::byte>("ec0adff257bbfe500c188c80b4fdd640f6b45a482bbc15fc7cef5931deff0aa186f6eb9bba7b85dc4dcc28b28722de1e3d9108b985e2967045668f66098e475b");
    const auto in_ellswift_theirs = ParseHex<std::byte>("a4a94dfce69b4a2a0a099313d10f9f7e7d649d60501c9e1d274c300e0d89aafaffffffffffffffffffffffffffffffffffffffffffffffffffffffff8faf88d5");
    const bool in_initiating = true;
    const auto in_contents = ParseHex<std::byte>("8e");
    
    // Expected outputs (OpenSY-specific, derived using HKDF salt: "opensy_v2_shared_secret" + mainnet magic)
    const auto expected_send_garbage = ParseHex<std::byte>("538cdb8e49cd7db0c074d7c1c1935ed1");
    const auto expected_recv_garbage = ParseHex<std::byte>("e4e8487cbd97c889b4cb0390d0ecaf49");
    const auto expected_session_id = ParseHex<std::byte>("5d037736bbded8981af8bb42c07760e910f95b97cd1656d6b5117840009e3607");
    const auto expected_ciphertext = ParseHex<std::byte>("18ecdad7b3a92a3dbba220ec75323010af6183e002");

    // Load keys
    CKey key;
    key.Set(in_priv_ours.begin(), in_priv_ours.end(), true);
    EllSwiftPubKey ellswift_ours(in_ellswift_ours);
    EllSwiftPubKey ellswift_theirs(in_ellswift_theirs);

    // Instantiate encryption BIP324 cipher
    BIP324Cipher cipher(key, ellswift_ours);
    BOOST_CHECK(!cipher);
    BOOST_CHECK(cipher.GetOurPubKey() == ellswift_ours);
    cipher.Initialize(ellswift_theirs, in_initiating);
    BOOST_CHECK(cipher);

    // Compare session variables
    BOOST_CHECK(std::ranges::equal(expected_session_id, cipher.GetSessionID()));
    BOOST_CHECK(std::ranges::equal(expected_send_garbage, cipher.GetSendGarbageTerminator()));
    BOOST_CHECK(std::ranges::equal(expected_recv_garbage, cipher.GetReceiveGarbageTerminator()));

    // Seek to the numbered packet (idx=1, so encrypt one dummy first)
    for (uint32_t i = 0; i < in_idx; ++i) {
        std::vector<std::byte> dummy(cipher.EXPANSION);
        cipher.Encrypt({}, {}, true, dummy);
    }

    // Encrypt contents
    std::vector<std::byte> ciphertext(in_contents.size() + cipher.EXPANSION);
    cipher.Encrypt(in_contents, {}, false, ciphertext);

    // Verify ciphertext matches expected
    BOOST_CHECK(ciphertext == expected_ciphertext);
}


BOOST_AUTO_TEST_SUITE_END()
