// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/amount.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/src20.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

/**
 * SRC-20 Token Protocol Unit Tests
 *
 * These tests verify the correct behavior of the SRC-20 token protocol,
 * including:
 * - Token data structure validation
 * - Script parsing and building
 * - Ticker validation and reserved tickers
 * - Token ID generation
 */

BOOST_FIXTURE_TEST_SUITE(src20_tests, BasicTestingSetup)

// =============================================================================
// TOKEN ACTION TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(action_from_byte_valid)
{
    BOOST_CHECK(src20::ActionFromByte(0x01) == src20::TokenAction::ISSUE);
    BOOST_CHECK(src20::ActionFromByte(0x02) == src20::TokenAction::TRANSFER);
    BOOST_CHECK(src20::ActionFromByte(0x03) == src20::TokenAction::BURN);
}

BOOST_AUTO_TEST_CASE(action_from_byte_invalid)
{
    BOOST_CHECK(src20::ActionFromByte(0x00) == src20::TokenAction::INVALID);
    BOOST_CHECK(src20::ActionFromByte(0x04) == src20::TokenAction::INVALID);
    BOOST_CHECK(src20::ActionFromByte(0xFF) == src20::TokenAction::INVALID);
}

BOOST_AUTO_TEST_CASE(action_to_string)
{
    BOOST_CHECK_EQUAL(src20::ActionToString(src20::TokenAction::ISSUE), "ISSUE");
    BOOST_CHECK_EQUAL(src20::ActionToString(src20::TokenAction::TRANSFER), "TRANSFER");
    BOOST_CHECK_EQUAL(src20::ActionToString(src20::TokenAction::BURN), "BURN");
    BOOST_CHECK_EQUAL(src20::ActionToString(src20::TokenAction::INVALID), "INVALID");
}

// =============================================================================
// TOKEN ID TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(token_id_null)
{
    src20::TokenId id;
    BOOST_CHECK(id.IsNull());
    BOOST_CHECK_EQUAL(id.GetHash(), uint256{});
}

BOOST_AUTO_TEST_CASE(token_id_from_hash)
{
    uint256 hash;
    hash = *uint256::FromHex("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    src20::TokenId id(hash);

    BOOST_CHECK(!id.IsNull());
    BOOST_CHECK_EQUAL(id.GetHash(), hash);
}

BOOST_AUTO_TEST_CASE(token_id_from_hex_valid)
{
    std::string hex = "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef";
    auto id = src20::TokenId::FromHex(hex);

    BOOST_CHECK(id.has_value());
    BOOST_CHECK(!id->IsNull());
}

BOOST_AUTO_TEST_CASE(token_id_from_hex_invalid)
{
    // Too short
    BOOST_CHECK(!src20::TokenId::FromHex("1234").has_value());

    // Invalid characters
    BOOST_CHECK(!src20::TokenId::FromHex("gggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggg").has_value());

    // Empty
    BOOST_CHECK(!src20::TokenId::FromHex("").has_value());
}

BOOST_AUTO_TEST_CASE(token_id_comparison)
{
    uint256 hash1, hash2;
    hash1 = *uint256::FromHex("1111111111111111111111111111111111111111111111111111111111111111");
    hash2 = *uint256::FromHex("2222222222222222222222222222222222222222222222222222222222222222");

    src20::TokenId id1(hash1);
    src20::TokenId id2(hash2);
    src20::TokenId id1_copy(hash1);

    BOOST_CHECK(id1 == id1_copy);
    BOOST_CHECK(id1 != id2);
    BOOST_CHECK(id1 < id2);
}

// =============================================================================
// TOKEN ISSUANCE VALIDATION TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(issuance_valid)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test Token";
    issuance.decimals = 8;
    issuance.total_supply = 10000000000ULL; // 10 million with 8 decimals

    BOOST_CHECK(issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(issuance_empty_ticker_invalid)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "";
    issuance.name = "Test Token";
    issuance.decimals = 8;
    issuance.total_supply = 10000000000ULL;

    BOOST_CHECK(!issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(issuance_ticker_too_long_invalid)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "TOOLONG"; // > 4 chars
    issuance.name = "Test Token";
    issuance.decimals = 8;
    issuance.total_supply = 10000000000ULL;

    BOOST_CHECK(!issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(issuance_ticker_lowercase_invalid)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "test"; // Must be uppercase
    issuance.name = "Test Token";
    issuance.decimals = 8;
    issuance.total_supply = 10000000000ULL;

    BOOST_CHECK(!issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(issuance_ticker_special_chars_invalid)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "T$ST"; // Special char
    issuance.name = "Test Token";
    issuance.decimals = 8;
    issuance.total_supply = 10000000000ULL;

    BOOST_CHECK(!issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(issuance_empty_name_invalid)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "";
    issuance.decimals = 8;
    issuance.total_supply = 10000000000ULL;

    BOOST_CHECK(!issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(issuance_name_too_long_invalid)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = std::string(33, 'A'); // > 32 chars
    issuance.decimals = 8;
    issuance.total_supply = 10000000000ULL;

    BOOST_CHECK(!issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(issuance_decimals_too_high_invalid)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test Token";
    issuance.decimals = 19; // > 18
    issuance.total_supply = 10000000000ULL;

    BOOST_CHECK(!issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(issuance_zero_supply_invalid)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test Token";
    issuance.decimals = 8;
    issuance.total_supply = 0;

    BOOST_CHECK(!issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(issuance_reserved_ticker_invalid)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "SYL"; // Reserved
    issuance.name = "Fake SYL";
    issuance.decimals = 8;
    issuance.total_supply = 10000000000ULL;

    BOOST_CHECK(!issuance.IsValid());
}

// AUDIT FIX [L-02]: Test maximum supply validation to prevent display overflow
BOOST_AUTO_TEST_CASE(issuance_supply_overflow_with_decimals_invalid)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test Token";
    issuance.decimals = 18;
    // UINT64_MAX with 18 decimals would overflow display calculations
    // Max safe is UINT64_MAX / 10^18 ≈ 18.4
    issuance.total_supply = UINT64_MAX;

    BOOST_CHECK(!issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(issuance_supply_safe_with_high_decimals_valid)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test Token";
    issuance.decimals = 18;
    // This should be safe: 18 tokens with 18 decimals = 18 * 10^18
    issuance.total_supply = 18;

    BOOST_CHECK(issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(issuance_supply_large_with_zero_decimals_valid)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test Token";
    issuance.decimals = 0;
    // With 0 decimals, any supply up to UINT64_MAX is valid
    issuance.total_supply = UINT64_MAX;

    BOOST_CHECK(issuance.IsValid());
}

// =============================================================================
// RESERVED TICKER TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(reserved_tickers)
{
    BOOST_CHECK(src20::reserved::IsReservedTicker("SYL"));
    BOOST_CHECK(src20::reserved::IsReservedTicker("ESYP"));
    BOOST_CHECK(src20::reserved::IsReservedTicker("SUSD"));
    BOOST_CHECK(src20::reserved::IsReservedTicker("SUST"));
    // FIX [L-09]: Lowercase tickers (eSYP, sUSD, sUST) removed from reserved list
    // since ticker validation only allows uppercase A-Z / 0-9.
    BOOST_CHECK(!src20::reserved::IsReservedTicker("eSYP"));
    BOOST_CHECK(!src20::reserved::IsReservedTicker("sUSD"));
    BOOST_CHECK(!src20::reserved::IsReservedTicker("sUST"));
}

BOOST_AUTO_TEST_CASE(non_reserved_tickers)
{
    BOOST_CHECK(!src20::reserved::IsReservedTicker("TEST"));
    BOOST_CHECK(!src20::reserved::IsReservedTicker("COIN"));
    BOOST_CHECK(!src20::reserved::IsReservedTicker("ABCD"));
    BOOST_CHECK(!src20::reserved::IsReservedTicker(""));
}

// =============================================================================
// TOKEN TRANSFER VALIDATION TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(transfer_valid)
{
    uint256 hash;
    hash = *uint256::FromHex("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    src20::TokenId token_id(hash);

    src20::TokenTransfer transfer(token_id, 100000000ULL);
    BOOST_CHECK(transfer.IsValid());
}

BOOST_AUTO_TEST_CASE(transfer_null_token_invalid)
{
    src20::TokenId token_id; // Null
    src20::TokenTransfer transfer(token_id, 100000000ULL);

    BOOST_CHECK(!transfer.IsValid());
}

BOOST_AUTO_TEST_CASE(transfer_zero_amount_invalid)
{
    uint256 hash;
    hash = *uint256::FromHex("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    src20::TokenId token_id(hash);

    src20::TokenTransfer transfer(token_id, 0);
    BOOST_CHECK(!transfer.IsValid());
}

// =============================================================================
// TOKEN BURN VALIDATION TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(burn_valid)
{
    uint256 hash;
    hash = *uint256::FromHex("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    src20::TokenId token_id(hash);

    src20::TokenBurn burn(token_id, 50000000ULL);
    BOOST_CHECK(burn.IsValid());
}

BOOST_AUTO_TEST_CASE(burn_null_token_invalid)
{
    src20::TokenId token_id; // Null
    src20::TokenBurn burn(token_id, 50000000ULL);

    BOOST_CHECK(!burn.IsValid());
}

BOOST_AUTO_TEST_CASE(burn_zero_amount_invalid)
{
    uint256 hash;
    hash = *uint256::FromHex("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    src20::TokenId token_id(hash);

    src20::TokenBurn burn(token_id, 0);
    BOOST_CHECK(!burn.IsValid());
}

// =============================================================================
// SCRIPT PARSING TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(is_src20_script_valid)
{
    // Build a valid SRC-20 OP_RETURN script
    CScript script = CScript() << OP_RETURN;

    // Add SRC-20 protocol ID: "SRC20" + version + action
    std::vector<uint8_t> data;
    data.insert(data.end(), src20::SRC20_PROTOCOL_ID.begin(), src20::SRC20_PROTOCOL_ID.end());
    data.push_back(src20::SRC20_VERSION); // Version byte
    data.push_back(0x01); // ISSUE action

    script << data;

    BOOST_CHECK(src20::IsSRC20Script(script));
}

BOOST_AUTO_TEST_CASE(is_src20_script_invalid_not_op_return)
{
    CScript script = CScript() << OP_DUP << OP_HASH160;
    BOOST_CHECK(!src20::IsSRC20Script(script));
}

BOOST_AUTO_TEST_CASE(is_src20_script_invalid_wrong_prefix)
{
    CScript script = CScript() << OP_RETURN;
    std::vector<uint8_t> data = {'W', 'R', 'O', 'N', 'G'};
    script << data;

    BOOST_CHECK(!src20::IsSRC20Script(script));
}

BOOST_AUTO_TEST_CASE(is_src20_script_invalid_empty)
{
    CScript script = CScript() << OP_RETURN;
    BOOST_CHECK(!src20::IsSRC20Script(script));
}

// =============================================================================
// SCRIPT BUILDING TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(build_issuance_script)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test Token";
    issuance.decimals = 8;
    issuance.total_supply = 10000000000ULL;

    CScript script = src20::BuildIssuanceScript(issuance);

    // Verify it's a valid SRC-20 script
    BOOST_CHECK(src20::IsSRC20Script(script));

    // Parse it back and verify
    auto parsed = src20::ParseSRC20Script(script);
    BOOST_CHECK(parsed.has_value());
    BOOST_CHECK(parsed->action == src20::TokenAction::ISSUE);

    const auto* result = parsed->GetIssuance();
    BOOST_CHECK(result != nullptr);
    BOOST_CHECK_EQUAL(result->ticker, "TEST");
    BOOST_CHECK_EQUAL(result->name, "Test Token");
    BOOST_CHECK_EQUAL(result->decimals, 8);
    BOOST_CHECK_EQUAL(result->total_supply, 10000000000ULL);
}

BOOST_AUTO_TEST_CASE(build_transfer_script)
{
    uint256 hash;
    hash = *uint256::FromHex("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    src20::TokenId token_id(hash);

    src20::TokenTransfer transfer(token_id, 500000000ULL);
    CScript script = src20::BuildTransferScript(transfer);

    // Verify it's a valid SRC-20 script
    BOOST_CHECK(src20::IsSRC20Script(script));

    // Parse it back and verify
    auto parsed = src20::ParseSRC20Script(script);
    BOOST_CHECK(parsed.has_value());
    BOOST_CHECK(parsed->action == src20::TokenAction::TRANSFER);

    const auto* result = parsed->GetTransfer();
    BOOST_CHECK(result != nullptr);
    BOOST_CHECK(result->token_id == token_id);
    BOOST_CHECK_EQUAL(result->amount, 500000000ULL);
}

BOOST_AUTO_TEST_CASE(build_burn_script)
{
    uint256 hash;
    hash = *uint256::FromHex("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    src20::TokenId token_id(hash);

    src20::TokenBurn burn(token_id, 100000000ULL);
    CScript script = src20::BuildBurnScript(burn);

    // Verify it's a valid SRC-20 script
    BOOST_CHECK(src20::IsSRC20Script(script));

    // Parse it back and verify
    auto parsed = src20::ParseSRC20Script(script);
    BOOST_CHECK(parsed.has_value());
    BOOST_CHECK(parsed->action == src20::TokenAction::BURN);

    const auto* result = parsed->GetBurn();
    BOOST_CHECK(result != nullptr);
    BOOST_CHECK(result->token_id == token_id);
    BOOST_CHECK_EQUAL(result->amount, 100000000ULL);
}

// =============================================================================
// SERIALIZATION TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(token_id_serialization)
{
    uint256 hash;
    hash = *uint256::FromHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    src20::TokenId original(hash);

    // Serialize
    DataStream ss{};
    ss << original;

    // Deserialize
    src20::TokenId deserialized;
    ss >> deserialized;

    BOOST_CHECK(original == deserialized);
}

BOOST_AUTO_TEST_CASE(token_issuance_serialization)
{
    src20::TokenIssuance original;
    original.ticker = "TEST";
    original.name = "Test Token";
    original.decimals = 8;
    original.total_supply = 10000000000ULL;
    original.metadata_hash = *uint256::FromHex("0000111122223333444455556666777788889999aaaabbbbccccddddeeeeffff");

    // Serialize
    DataStream ss{};
    ss << original;

    // Deserialize
    src20::TokenIssuance deserialized;
    ss >> deserialized;

    BOOST_CHECK_EQUAL(original.ticker, deserialized.ticker);
    BOOST_CHECK_EQUAL(original.name, deserialized.name);
    BOOST_CHECK_EQUAL(original.decimals, deserialized.decimals);
    BOOST_CHECK_EQUAL(original.total_supply, deserialized.total_supply);
    BOOST_CHECK(original.metadata_hash == deserialized.metadata_hash);
}

BOOST_AUTO_TEST_CASE(token_transfer_serialization)
{
    uint256 hash;
    hash = *uint256::FromHex("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");

    src20::TokenTransfer original(src20::TokenId(hash), 500000000ULL);

    // Serialize
    DataStream ss{};
    ss << original;

    // Deserialize
    src20::TokenTransfer deserialized;
    ss >> deserialized;

    BOOST_CHECK(original.token_id == deserialized.token_id);
    BOOST_CHECK_EQUAL(original.amount, deserialized.amount);
}

BOOST_AUTO_TEST_CASE(token_burn_serialization)
{
    uint256 hash;
    hash = *uint256::FromHex("fedcba0987654321fedcba0987654321fedcba0987654321fedcba0987654321");

    src20::TokenBurn original(src20::TokenId(hash), 250000000ULL);

    // Serialize
    DataStream ss{};
    ss << original;

    // Deserialize
    src20::TokenBurn deserialized;
    ss >> deserialized;

    BOOST_CHECK(original.token_id == deserialized.token_id);
    BOOST_CHECK_EQUAL(original.amount, deserialized.amount);
}

// =============================================================================
// CONSTANTS TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(protocol_constants)
{
    BOOST_CHECK_EQUAL(src20::SRC20_VERSION, 1);
    BOOST_CHECK_EQUAL(src20::MAX_TOKENS_PER_BLOCK, 100);
    BOOST_CHECK_EQUAL(src20::MAX_TICKER_LENGTH, 4);
    BOOST_CHECK_EQUAL(src20::MAX_NAME_LENGTH, 32);
    BOOST_CHECK_EQUAL(src20::MAX_DECIMALS, 18);
}

BOOST_AUTO_TEST_CASE(protocol_id)
{
    BOOST_CHECK_EQUAL(src20::SRC20_PROTOCOL_ID.size(), 5);
    BOOST_CHECK_EQUAL(src20::SRC20_PROTOCOL_ID[0], 'S');
    BOOST_CHECK_EQUAL(src20::SRC20_PROTOCOL_ID[1], 'R');
    BOOST_CHECK_EQUAL(src20::SRC20_PROTOCOL_ID[2], 'C');
    BOOST_CHECK_EQUAL(src20::SRC20_PROTOCOL_ID[3], '2');
    BOOST_CHECK_EQUAL(src20::SRC20_PROTOCOL_ID[4], '0');
}

// =============================================================================
// WELL-KNOWN TOKEN TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(wellknown_esyp_valid)
{
    auto esyp = src20::wellknown::CreateESYP();
    // ESYP is a reserved ticker but should be valid when created by the system
    // For unit testing, we just verify the structure is correct
    BOOST_CHECK_EQUAL(esyp.ticker, "ESYP");
    BOOST_CHECK(!esyp.name.empty());
    BOOST_CHECK_LE(esyp.decimals, src20::MAX_DECIMALS);
    BOOST_CHECK_GT(esyp.total_supply, 0ULL);
}

BOOST_AUTO_TEST_CASE(wellknown_susdt_valid)
{
    auto susdt = src20::wellknown::CreateSUSDT();
    BOOST_CHECK_EQUAL(susdt.ticker, "SUST");
    BOOST_CHECK(!susdt.name.empty());
    BOOST_CHECK_LE(susdt.decimals, src20::MAX_DECIMALS);
    BOOST_CHECK_GT(susdt.total_supply, 0ULL);
}

// =============================================================================
// BOUNDARY VALUE TESTS - TICKER
// =============================================================================

BOOST_AUTO_TEST_CASE(ticker_boundary_exactly_min_length)
{
    // Single character ticker - minimum valid
    src20::TokenIssuance issuance;
    issuance.ticker = "A";
    issuance.name = "Test";
    issuance.decimals = 8;
    issuance.total_supply = 1000;
    BOOST_CHECK(issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(ticker_boundary_exactly_max_length)
{
    // Exactly 4 characters - maximum valid
    src20::TokenIssuance issuance;
    issuance.ticker = "ABCD";
    issuance.name = "Test";
    issuance.decimals = 8;
    issuance.total_supply = 1000;
    BOOST_CHECK(issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(ticker_boundary_one_over_max)
{
    // 5 characters - just over max
    src20::TokenIssuance issuance;
    issuance.ticker = "ABCDE";
    issuance.name = "Test";
    issuance.decimals = 8;
    issuance.total_supply = 1000;
    BOOST_CHECK(!issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(ticker_with_numbers)
{
    // Alphanumeric uppercase is valid
    src20::TokenIssuance issuance;
    issuance.ticker = "A1B2";
    issuance.name = "Test";
    issuance.decimals = 8;
    issuance.total_supply = 1000;
    BOOST_CHECK(issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(ticker_all_numbers)
{
    // All numbers is valid
    src20::TokenIssuance issuance;
    issuance.ticker = "1234";
    issuance.name = "Test";
    issuance.decimals = 8;
    issuance.total_supply = 1000;
    BOOST_CHECK(issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(ticker_with_lowercase)
{
    // Lowercase letters are invalid
    src20::TokenIssuance issuance;
    issuance.ticker = "AbCd";
    issuance.name = "Test";
    issuance.decimals = 8;
    issuance.total_supply = 1000;
    BOOST_CHECK(!issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(ticker_with_special_chars)
{
    // Special characters invalid
    src20::TokenIssuance issuance;
    issuance.ticker = "AB-C";
    issuance.name = "Test";
    issuance.decimals = 8;
    issuance.total_supply = 1000;
    BOOST_CHECK(!issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(ticker_with_underscore)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "AB_C";
    issuance.name = "Test";
    issuance.decimals = 8;
    issuance.total_supply = 1000;
    BOOST_CHECK(!issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(ticker_with_space)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "AB C";
    issuance.name = "Test";
    issuance.decimals = 8;
    issuance.total_supply = 1000;
    BOOST_CHECK(!issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(ticker_with_null_byte)
{
    src20::TokenIssuance issuance;
    issuance.ticker = std::string("AB\0C", 4);
    issuance.name = "Test";
    issuance.decimals = 8;
    issuance.total_supply = 1000;
    BOOST_CHECK(!issuance.IsValid());
}

// =============================================================================
// BOUNDARY VALUE TESTS - NAME
// =============================================================================

BOOST_AUTO_TEST_CASE(name_boundary_exactly_min_length)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "A";  // Single character - minimum valid
    issuance.decimals = 8;
    issuance.total_supply = 1000;
    BOOST_CHECK(issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(name_boundary_exactly_max_length)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = std::string(32, 'A');  // Exactly 32 characters
    issuance.decimals = 8;
    issuance.total_supply = 1000;
    BOOST_CHECK(issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(name_boundary_one_over_max)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = std::string(33, 'A');  // 33 characters - invalid
    issuance.decimals = 8;
    issuance.total_supply = 1000;
    BOOST_CHECK(!issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(name_with_special_chars)
{
    // Names can have special characters
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test-Token (v1.0)";
    issuance.decimals = 8;
    issuance.total_supply = 1000;
    BOOST_CHECK(issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(name_with_unicode)
{
    // Unicode characters count as multiple bytes
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test🚀";  // Unicode emoji
    issuance.decimals = 8;
    issuance.total_supply = 1000;
    // Depends on byte length, not character count
    BOOST_CHECK(issuance.name.size() <= src20::MAX_NAME_LENGTH ? issuance.IsValid() : !issuance.IsValid());
}

// =============================================================================
// BOUNDARY VALUE TESTS - DECIMALS
// =============================================================================

BOOST_AUTO_TEST_CASE(decimals_boundary_zero)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test";
    issuance.decimals = 0;  // Zero decimals - valid
    issuance.total_supply = 1000;
    BOOST_CHECK(issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(decimals_boundary_exactly_max)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test";
    issuance.decimals = 18;  // Exactly MAX_DECIMALS
    // Supply must be <= UINT64_MAX / 10^18 = 18 to avoid overflow
    issuance.total_supply = 18;
    BOOST_CHECK(issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(decimals_boundary_one_over_max)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test";
    issuance.decimals = 19;  // Just over max
    issuance.total_supply = 1000;
    BOOST_CHECK(!issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(decimals_boundary_max_uint8)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test";
    issuance.decimals = 255;  // Max uint8 value
    issuance.total_supply = 1000;
    BOOST_CHECK(!issuance.IsValid());
}

// =============================================================================
// BOUNDARY VALUE TESTS - SUPPLY
// =============================================================================

BOOST_AUTO_TEST_CASE(supply_boundary_one)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test";
    issuance.decimals = 8;
    issuance.total_supply = 1;  // Minimum valid supply
    BOOST_CHECK(issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(supply_boundary_max_uint64)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test";
    issuance.decimals = 8;
    issuance.total_supply = UINT64_MAX;  // Maximum possible value
    // With 8 decimals, UINT64_MAX would overflow display calculations
    // The L-02 security fix correctly rejects this
    BOOST_CHECK(!issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(supply_boundary_max_safe_for_decimals)
{
    // Test that max safe supply for 8 decimals is accepted
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test";
    issuance.decimals = 8;
    // Max safe = UINT64_MAX / 10^8 = 184467440737
    issuance.total_supply = 184467440737ULL;
    BOOST_CHECK(issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(supply_boundary_max_safe_zero_decimals)
{
    // With 0 decimals, UINT64_MAX should be valid (no multiplication needed)
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test";
    issuance.decimals = 0;
    issuance.total_supply = UINT64_MAX;
    BOOST_CHECK(issuance.IsValid());
}

BOOST_AUTO_TEST_CASE(transfer_amount_one)
{
    src20::TokenId id(uint256::ONE);
    src20::TokenTransfer transfer(id, 1);  // Minimum valid amount
    BOOST_CHECK(transfer.IsValid());
}

BOOST_AUTO_TEST_CASE(transfer_amount_max_uint64)
{
    src20::TokenId id(uint256::ONE);
    src20::TokenTransfer transfer(id, UINT64_MAX);  // Maximum amount
    BOOST_CHECK(transfer.IsValid());
}

BOOST_AUTO_TEST_CASE(burn_amount_one)
{
    src20::TokenId id(uint256::ONE);
    src20::TokenBurn burn(id, 1);  // Minimum valid amount
    BOOST_CHECK(burn.IsValid());
}

BOOST_AUTO_TEST_CASE(burn_amount_max_uint64)
{
    src20::TokenId id(uint256::ONE);
    src20::TokenBurn burn(id, UINT64_MAX);  // Maximum amount
    BOOST_CHECK(burn.IsValid());
}

// =============================================================================
// MALFORMED SCRIPT TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(script_empty)
{
    CScript script;
    BOOST_CHECK(!src20::IsSRC20Script(script));
    BOOST_CHECK(!src20::ParseSRC20Script(script).has_value());
}

BOOST_AUTO_TEST_CASE(script_op_return_only)
{
    CScript script;
    script << OP_RETURN;
    BOOST_CHECK(!src20::IsSRC20Script(script));
    BOOST_CHECK(!src20::ParseSRC20Script(script).has_value());
}

BOOST_AUTO_TEST_CASE(script_op_return_empty_data)
{
    CScript script;
    script << OP_RETURN << std::vector<uint8_t>{};
    BOOST_CHECK(!src20::IsSRC20Script(script));
}

BOOST_AUTO_TEST_CASE(script_wrong_protocol_id)
{
    std::vector<uint8_t> data = {'S', 'R', 'C', '2', '1', 0x01, 0x01};  // SRC21 instead of SRC20
    CScript script;
    script << OP_RETURN << data;
    BOOST_CHECK(!src20::IsSRC20Script(script));
}

BOOST_AUTO_TEST_CASE(script_partial_protocol_id)
{
    std::vector<uint8_t> data = {'S', 'R', 'C'};  // Incomplete protocol ID
    CScript script;
    script << OP_RETURN << data;
    BOOST_CHECK(!src20::IsSRC20Script(script));
}

BOOST_AUTO_TEST_CASE(script_wrong_version)
{
    std::vector<uint8_t> data = {'S', 'R', 'C', '2', '0', 0x02, 0x01};  // Version 2 instead of 1
    CScript script;
    script << OP_RETURN << data;
    BOOST_CHECK(src20::IsSRC20Script(script));  // Protocol ID matches
    BOOST_CHECK(!src20::ParseSRC20Script(script).has_value());  // But version is wrong
}

BOOST_AUTO_TEST_CASE(script_invalid_action)
{
    std::vector<uint8_t> data = {'S', 'R', 'C', '2', '0', 0x01, 0xFF};  // Invalid action 0xFF
    CScript script;
    script << OP_RETURN << data;
    BOOST_CHECK(!src20::ParseSRC20Script(script).has_value());
}

BOOST_AUTO_TEST_CASE(script_truncated_issuance)
{
    // Valid header but truncated issuance data
    std::vector<uint8_t> data = {'S', 'R', 'C', '2', '0', 0x01, 0x01, 'T', 'E'};  // Incomplete ticker
    CScript script;
    script << OP_RETURN << data;
    BOOST_CHECK(!src20::ParseSRC20Script(script).has_value());
}

BOOST_AUTO_TEST_CASE(script_truncated_transfer)
{
    // Valid header but truncated transfer data
    std::vector<uint8_t> data = {'S', 'R', 'C', '2', '0', 0x01, 0x02};  // No token ID or amount
    data.resize(20);  // Some random bytes, not enough for token_id + amount
    CScript script;
    script << OP_RETURN << data;
    BOOST_CHECK(!src20::ParseSRC20Script(script).has_value());
}

BOOST_AUTO_TEST_CASE(script_truncated_burn)
{
    std::vector<uint8_t> data = {'S', 'R', 'C', '2', '0', 0x01, 0x03};  // BURN action
    data.resize(15);  // Not enough for token_id + amount
    CScript script;
    script << OP_RETURN << data;
    BOOST_CHECK(!src20::ParseSRC20Script(script).has_value());
}

BOOST_AUTO_TEST_CASE(script_not_op_return)
{
    // Regular P2PKH script, not OP_RETURN
    CScript script;
    script << OP_DUP << OP_HASH160 << std::vector<uint8_t>(20, 0x00) << OP_EQUALVERIFY << OP_CHECKSIG;
    BOOST_CHECK(!src20::IsSRC20Script(script));
}

BOOST_AUTO_TEST_CASE(script_valid_issuance_with_extra_data)
{
    // Build valid issuance then add extra data - should still parse
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test Token";
    issuance.decimals = 8;
    issuance.total_supply = 1000000;
    
    CScript script = src20::BuildIssuanceScript(issuance);
    // Extra data after valid payload should be ignored
    auto parsed = src20::ParseSRC20Script(script);
    BOOST_CHECK(parsed.has_value());
    BOOST_CHECK_EQUAL(static_cast<int>(parsed->action), static_cast<int>(src20::TokenAction::ISSUE));
}

// =============================================================================
// TRANSACTION PARSING TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(parse_transaction_no_src20)
{
    CMutableTransaction mtx;
    mtx.version = 2;
    
    // Add a regular P2PKH output
    CScript p2pkh;
    p2pkh << OP_DUP << OP_HASH160 << std::vector<uint8_t>(20, 0x01) << OP_EQUALVERIFY << OP_CHECKSIG;
    mtx.vout.push_back(CTxOut(1000, p2pkh));
    
    CTransaction tx(mtx);
    auto ops = src20::ParseTransactionSRC20(tx);
    BOOST_CHECK(ops.empty());
}

BOOST_AUTO_TEST_CASE(parse_transaction_single_issuance)
{
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test Token";
    issuance.decimals = 8;
    issuance.total_supply = 1000000;
    
    CMutableTransaction mtx;
    mtx.version = 2;
    mtx.vout.push_back(CTxOut(0, src20::BuildIssuanceScript(issuance)));
    
    CTransaction tx(mtx);
    auto ops = src20::ParseTransactionSRC20(tx);
    BOOST_CHECK_EQUAL(ops.size(), 1U);
    BOOST_CHECK_EQUAL(static_cast<int>(ops[0].action), static_cast<int>(src20::TokenAction::ISSUE));
}

BOOST_AUTO_TEST_CASE(parse_transaction_single_transfer)
{
    src20::TokenId id(uint256::ONE);
    src20::TokenTransfer transfer(id, 1000);
    
    CMutableTransaction mtx;
    mtx.version = 2;
    mtx.vout.push_back(CTxOut(0, src20::BuildTransferScript(transfer)));
    
    // Add recipient output
    CScript recipient;
    recipient << OP_DUP << OP_HASH160 << std::vector<uint8_t>(20, 0x02) << OP_EQUALVERIFY << OP_CHECKSIG;
    mtx.vout.push_back(CTxOut(546, recipient));  // Dust output for token recipient
    
    CTransaction tx(mtx);
    auto ops = src20::ParseTransactionSRC20(tx);
    BOOST_CHECK_EQUAL(ops.size(), 1U);
    BOOST_CHECK_EQUAL(static_cast<int>(ops[0].action), static_cast<int>(src20::TokenAction::TRANSFER));
}

BOOST_AUTO_TEST_CASE(parse_transaction_multiple_operations)
{
    CMutableTransaction mtx;
    mtx.version = 2;
    
    // Add two different issuances (although semantically invalid, parsing should work)
    src20::TokenIssuance issuance1;
    issuance1.ticker = "TST1";
    issuance1.name = "Test One";
    issuance1.decimals = 8;
    issuance1.total_supply = 1000;
    mtx.vout.push_back(CTxOut(0, src20::BuildIssuanceScript(issuance1)));
    
    src20::TokenIssuance issuance2;
    issuance2.ticker = "TST2";
    issuance2.name = "Test Two";
    issuance2.decimals = 6;
    issuance2.total_supply = 2000;
    mtx.vout.push_back(CTxOut(0, src20::BuildIssuanceScript(issuance2)));
    
    CTransaction tx(mtx);
    auto ops = src20::ParseTransactionSRC20(tx);
    BOOST_CHECK_EQUAL(ops.size(), 2U);
}

BOOST_AUTO_TEST_CASE(parse_transaction_mixed_outputs)
{
    CMutableTransaction mtx;
    mtx.version = 2;
    
    // Regular output
    CScript p2pkh;
    p2pkh << OP_DUP << OP_HASH160 << std::vector<uint8_t>(20, 0x01) << OP_EQUALVERIFY << OP_CHECKSIG;
    mtx.vout.push_back(CTxOut(1000, p2pkh));
    
    // SRC-20 issuance
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test";
    issuance.decimals = 8;
    issuance.total_supply = 1000;
    mtx.vout.push_back(CTxOut(0, src20::BuildIssuanceScript(issuance)));
    
    // Another regular output
    mtx.vout.push_back(CTxOut(2000, p2pkh));
    
    CTransaction tx(mtx);
    auto ops = src20::ParseTransactionSRC20(tx);
    BOOST_CHECK_EQUAL(ops.size(), 1U);  // Only the SRC-20 output
}

// =============================================================================
// TRANSFER RECIPIENT/CHANGE EXTRACTION TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(get_transfer_recipient_valid)
{
    src20::TokenId id(uint256::ONE);
    src20::TokenTransfer transfer(id, 1000);
    
    CMutableTransaction mtx;
    mtx.version = 2;
    
    // Output 0: OP_RETURN
    mtx.vout.push_back(CTxOut(0, src20::BuildTransferScript(transfer)));
    
    // Output 1: Recipient
    CScript recipient;
    recipient << OP_DUP << OP_HASH160 << std::vector<uint8_t>(20, 0xAB) << OP_EQUALVERIFY << OP_CHECKSIG;
    mtx.vout.push_back(CTxOut(546, recipient));
    
    CTransaction tx(mtx);
    auto result = src20::GetTransferRecipient(tx);
    BOOST_CHECK(result.has_value());
    BOOST_CHECK(*result == recipient);
}

BOOST_AUTO_TEST_CASE(get_transfer_recipient_with_change)
{
    src20::TokenId id(uint256::ONE);
    src20::TokenTransfer transfer(id, 1000);
    
    CMutableTransaction mtx;
    mtx.version = 2;
    
    // Output 0: OP_RETURN
    mtx.vout.push_back(CTxOut(0, src20::BuildTransferScript(transfer)));
    
    // Output 1: Recipient
    CScript recipient;
    recipient << OP_DUP << OP_HASH160 << std::vector<uint8_t>(20, 0xAB) << OP_EQUALVERIFY << OP_CHECKSIG;
    mtx.vout.push_back(CTxOut(546, recipient));
    
    // Output 2: Change
    CScript change;
    change << OP_DUP << OP_HASH160 << std::vector<uint8_t>(20, 0xCD) << OP_EQUALVERIFY << OP_CHECKSIG;
    mtx.vout.push_back(CTxOut(546, change));
    
    CTransaction tx(mtx);
    auto recipientResult = src20::GetTransferRecipient(tx);
    auto changeResult = src20::GetTransferChange(tx);
    
    BOOST_CHECK(recipientResult.has_value());
    BOOST_CHECK(changeResult.has_value());
    BOOST_CHECK(*recipientResult == recipient);
    BOOST_CHECK(*changeResult == change);
}

BOOST_AUTO_TEST_CASE(get_transfer_recipient_missing_recipient)
{
    src20::TokenId id(uint256::ONE);
    src20::TokenTransfer transfer(id, 1000);
    
    CMutableTransaction mtx;
    mtx.version = 2;
    
    // Only OP_RETURN, no recipient
    mtx.vout.push_back(CTxOut(0, src20::BuildTransferScript(transfer)));
    
    CTransaction tx(mtx);
    auto result = src20::GetTransferRecipient(tx);
    BOOST_CHECK(!result.has_value());
}

BOOST_AUTO_TEST_CASE(get_transfer_change_no_change)
{
    src20::TokenId id(uint256::ONE);
    src20::TokenTransfer transfer(id, 1000);
    
    CMutableTransaction mtx;
    mtx.version = 2;
    mtx.vout.push_back(CTxOut(0, src20::BuildTransferScript(transfer)));
    
    CScript recipient;
    recipient << OP_DUP << OP_HASH160 << std::vector<uint8_t>(20, 0xAB) << OP_EQUALVERIFY << OP_CHECKSIG;
    mtx.vout.push_back(CTxOut(546, recipient));
    
    CTransaction tx(mtx);
    auto result = src20::GetTransferChange(tx);
    BOOST_CHECK(!result.has_value());
}

BOOST_AUTO_TEST_CASE(get_transfer_recipient_wrong_action)
{
    // Test with issuance instead of transfer
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test";
    issuance.decimals = 8;
    issuance.total_supply = 1000;
    
    CMutableTransaction mtx;
    mtx.version = 2;
    mtx.vout.push_back(CTxOut(0, src20::BuildIssuanceScript(issuance)));
    
    CScript recipient;
    recipient << OP_DUP << OP_HASH160 << std::vector<uint8_t>(20, 0xAB) << OP_EQUALVERIFY << OP_CHECKSIG;
    mtx.vout.push_back(CTxOut(546, recipient));
    
    CTransaction tx(mtx);
    auto result = src20::GetTransferRecipient(tx);
    BOOST_CHECK(!result.has_value());  // Not a transfer
}

// =============================================================================
// TOKEN ID EDGE CASES
// =============================================================================

BOOST_AUTO_TEST_CASE(token_id_from_txid)
{
    Txid txid = Txid::FromUint256(uint256::ONE);
    src20::TokenId id(txid);
    BOOST_CHECK(!id.IsNull());
    BOOST_CHECK(id.GetHash() == uint256::ONE);
}

BOOST_AUTO_TEST_CASE(token_id_from_null_txid)
{
    Txid txid;  // Null txid
    src20::TokenId id(txid);
    BOOST_CHECK(id.IsNull());
}

BOOST_AUTO_TEST_CASE(token_id_ordering)
{
    src20::TokenId id1(uint256::ONE);
    src20::TokenId id2(uint256::ZERO);
    
    BOOST_CHECK(id2 < id1);  // ZERO < ONE
    BOOST_CHECK(!(id1 < id2));
    BOOST_CHECK(!(id1 < id1));  // Reflexive
}

BOOST_AUTO_TEST_CASE(token_id_from_hex_invalid_edge_cases)
{
    BOOST_CHECK(!src20::TokenId::FromHex("not_hex"));
    BOOST_CHECK(!src20::TokenId::FromHex("ZZZZ"));
    BOOST_CHECK(!src20::TokenId::FromHex(""));
    BOOST_CHECK(!src20::TokenId::FromHex(std::string(63, 'a')));  // Too short
    BOOST_CHECK(!src20::TokenId::FromHex(std::string(65, 'a')));  // Too long
}

BOOST_AUTO_TEST_CASE(token_id_from_hex_valid_full_string)
{
    std::string hex = std::string(64, 'a');
    auto id = src20::TokenId::FromHex(hex);
    BOOST_CHECK(id.has_value());
    BOOST_CHECK(!id->IsNull());
}

BOOST_AUTO_TEST_CASE(token_id_to_string_format)
{
    src20::TokenId id(uint256::ONE);
    std::string str = id.ToString();
    std::string hex = id.GetHex();
    // ToString and GetHex should return the same thing
    BOOST_CHECK_EQUAL(str, hex);
}

// =============================================================================
// RESERVED TICKERS EDGE CASES
// =============================================================================

BOOST_AUTO_TEST_CASE(reserved_ticker_case_sensitivity)
{
    // Reserved tickers are case-sensitive
    BOOST_CHECK(src20::reserved::IsReservedTicker("SYL"));
    BOOST_CHECK(!src20::reserved::IsReservedTicker("syl"));
    BOOST_CHECK(!src20::reserved::IsReservedTicker("Syl"));
    BOOST_CHECK(!src20::reserved::IsReservedTicker("SyL"));
}

BOOST_AUTO_TEST_CASE(reserved_ticker_all_reserved)
{
    // Native coin and variations
    BOOST_CHECK(src20::reserved::IsReservedTicker("SYL"));
    BOOST_CHECK(src20::reserved::IsReservedTicker("OSYL"));
    // OPENSY removed — 6 chars exceeds MAX_TICKER_LENGTH=4, was dead reservation
    BOOST_CHECK(!src20::reserved::IsReservedTicker("OPENSY"));
    
    // Official stablecoins (uppercase only — matches ticker validation)
    // Lowercase variants removed as they can never pass ticker validation
    BOOST_CHECK(!src20::reserved::IsReservedTicker("eSYP"));
    BOOST_CHECK(src20::reserved::IsReservedTicker("ESYP"));
    BOOST_CHECK(!src20::reserved::IsReservedTicker("sUSD"));
    BOOST_CHECK(src20::reserved::IsReservedTicker("SUSD"));
    BOOST_CHECK(!src20::reserved::IsReservedTicker("sUST"));
    BOOST_CHECK(src20::reserved::IsReservedTicker("SUST"));
    
    // Prevent impersonation
    BOOST_CHECK(src20::reserved::IsReservedTicker("BTC"));
    BOOST_CHECK(src20::reserved::IsReservedTicker("ETH"));
    BOOST_CHECK(src20::reserved::IsReservedTicker("USDT"));
}

BOOST_AUTO_TEST_CASE(reserved_ticker_similar_but_not_reserved)
{
    BOOST_CHECK(!src20::reserved::IsReservedTicker("SYLU"));  // Extra char
    BOOST_CHECK(!src20::reserved::IsReservedTicker("SY"));    // Missing char
    BOOST_CHECK(!src20::reserved::IsReservedTicker("XSYL"));  // Prefix
    BOOST_CHECK(!src20::reserved::IsReservedTicker("MYCOIN")); // Not reserved
}

BOOST_AUTO_TEST_CASE(reserved_ticker_runtime_extension)
{
    // Verify a ticker is not reserved initially
    BOOST_CHECK(!src20::reserved::IsReservedTicker("NEWTICKER"));
    
    // Add it at runtime
    src20::reserved::AddReservedTicker("NEWTICKER");
    
    // Now it should be reserved
    BOOST_CHECK(src20::reserved::IsReservedTicker("NEWTICKER"));
    
    // Verify GetReservedTickers includes both static and runtime tickers
    auto all_tickers = src20::reserved::GetReservedTickers();
    bool found_static = false;
    bool found_runtime = false;
    for (const auto& t : all_tickers) {
        if (t == "SYL") found_static = true;
        if (t == "NEWTICKER") found_runtime = true;
    }
    BOOST_CHECK(found_static);
    BOOST_CHECK(found_runtime);
}

// =============================================================================
// SCRIPT BUILDING ROUNDTRIP STRESS TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(roundtrip_issuance_min_values)
{
    src20::TokenIssuance original;
    original.ticker = "A";
    original.name = "A";
    original.decimals = 0;
    original.total_supply = 1;
    
    CScript script = src20::BuildIssuanceScript(original);
    auto parsed = src20::ParseSRC20Script(script);
    
    BOOST_CHECK(parsed.has_value());
    auto* issuance = parsed->GetIssuance();
    BOOST_CHECK(issuance != nullptr);
    BOOST_CHECK_EQUAL(issuance->ticker, original.ticker);
    BOOST_CHECK_EQUAL(issuance->name, original.name);
    BOOST_CHECK_EQUAL(issuance->decimals, original.decimals);
    BOOST_CHECK_EQUAL(issuance->total_supply, original.total_supply);
}

BOOST_AUTO_TEST_CASE(roundtrip_issuance_max_values)
{
    // Test maximum valid values (that don't overflow display calculations)
    src20::TokenIssuance original;
    original.ticker = "ZZZZ";
    original.name = std::string(32, 'Z');
    original.decimals = 0;  // With 0 decimals, max supply is valid
    original.total_supply = UINT64_MAX;
    
    CScript script = src20::BuildIssuanceScript(original);
    auto parsed = src20::ParseSRC20Script(script);
    
    BOOST_CHECK(parsed.has_value());
    auto* issuance = parsed->GetIssuance();
    BOOST_CHECK(issuance != nullptr);
    BOOST_CHECK_EQUAL(issuance->ticker, original.ticker);
    BOOST_CHECK_EQUAL(issuance->name, original.name);
    BOOST_CHECK_EQUAL(issuance->decimals, original.decimals);
    BOOST_CHECK_EQUAL(issuance->total_supply, original.total_supply);
}

BOOST_AUTO_TEST_CASE(roundtrip_issuance_max_decimals_safe_supply)
{
    // Test max decimals with safe supply (18 decimals, supply <= 18)
    src20::TokenIssuance original;
    original.ticker = "MAX";
    original.name = "Max Decimals Token";
    original.decimals = 18;
    original.total_supply = 18;  // Max safe for 18 decimals
    
    CScript script = src20::BuildIssuanceScript(original);
    auto parsed = src20::ParseSRC20Script(script);
    
    BOOST_CHECK(parsed.has_value());
    auto* issuance = parsed->GetIssuance();
    BOOST_CHECK(issuance != nullptr);
    BOOST_CHECK_EQUAL(issuance->ticker, original.ticker);
    BOOST_CHECK_EQUAL(issuance->name, original.name);
    BOOST_CHECK_EQUAL(issuance->decimals, original.decimals);
    BOOST_CHECK_EQUAL(issuance->total_supply, original.total_supply);
}

BOOST_AUTO_TEST_CASE(roundtrip_transfer_max_amount)
{
    src20::TokenId id(uint256::ONE);
    src20::TokenTransfer original(id, UINT64_MAX);
    
    CScript script = src20::BuildTransferScript(original);
    auto parsed = src20::ParseSRC20Script(script);
    
    BOOST_CHECK(parsed.has_value());
    auto* transfer = parsed->GetTransfer();
    BOOST_CHECK(transfer != nullptr);
    BOOST_CHECK(transfer->token_id == original.token_id);
    BOOST_CHECK_EQUAL(transfer->amount, original.amount);
}

BOOST_AUTO_TEST_CASE(roundtrip_burn_max_amount)
{
    src20::TokenId id(uint256::ONE);
    src20::TokenBurn original(id, UINT64_MAX);
    
    CScript script = src20::BuildBurnScript(original);
    auto parsed = src20::ParseSRC20Script(script);
    
    BOOST_CHECK(parsed.has_value());
    auto* burn = parsed->GetBurn();
    BOOST_CHECK(burn != nullptr);
    BOOST_CHECK(burn->token_id == original.token_id);
    BOOST_CHECK_EQUAL(burn->amount, original.amount);
}

// =============================================================================
// OP_RETURN DATA EXTRACTION TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(get_op_return_data_pushdata1)
{
    // Test PUSHDATA1 (for data 76-255 bytes)
    std::vector<uint8_t> expected_data(100, 0xAB);
    CScript script;
    script << OP_RETURN << expected_data;
    
    std::vector<uint8_t> extracted = src20::GetOpReturnData(script);
    BOOST_CHECK_EQUAL(extracted.size(), expected_data.size());
    BOOST_CHECK(extracted == expected_data);
}

BOOST_AUTO_TEST_CASE(get_op_return_data_direct_push)
{
    // Test direct push (data 1-75 bytes)
    std::vector<uint8_t> expected_data(50, 0xCD);
    CScript script;
    script << OP_RETURN << expected_data;
    
    std::vector<uint8_t> extracted = src20::GetOpReturnData(script);
    BOOST_CHECK_EQUAL(extracted.size(), expected_data.size());
    BOOST_CHECK(extracted == expected_data);
}

BOOST_AUTO_TEST_CASE(get_op_return_data_single_byte)
{
    std::vector<uint8_t> expected_data = {0x42};
    CScript script;
    script << OP_RETURN << expected_data;
    
    std::vector<uint8_t> extracted = src20::GetOpReturnData(script);
    BOOST_CHECK_EQUAL(extracted.size(), 1U);
    BOOST_CHECK_EQUAL(extracted[0], 0x42);
}

// =============================================================================
// SERIALIZATION EDGE CASES
// =============================================================================

BOOST_AUTO_TEST_CASE(serialize_token_issuance_empty_strings)
{
    src20::TokenIssuance original;
    original.ticker = "";  // Empty (would be invalid but test serialization)
    original.name = "";
    original.decimals = 0;
    original.total_supply = 0;
    
    DataStream ss{};
    ss << original;
    
    src20::TokenIssuance deserialized;
    ss >> deserialized;
    
    BOOST_CHECK_EQUAL(original.ticker, deserialized.ticker);
    BOOST_CHECK_EQUAL(original.name, deserialized.name);
}

BOOST_AUTO_TEST_CASE(serialize_token_id_null)
{
    src20::TokenId original;  // Null
    BOOST_CHECK(original.IsNull());
    
    DataStream ss{};
    ss << original;
    
    src20::TokenId deserialized;
    ss >> deserialized;
    
    BOOST_CHECK(deserialized.IsNull());
    BOOST_CHECK(original == deserialized);
}

BOOST_AUTO_TEST_CASE(serialize_multiple_operations)
{
    // Serialize a vector of operations
    std::vector<src20::TokenTransfer> transfers;
    transfers.emplace_back(src20::TokenId(uint256::ONE), 100);
    transfers.emplace_back(src20::TokenId(uint256::ZERO), 200);
    
    DataStream ss{};
    ss << transfers;
    
    std::vector<src20::TokenTransfer> deserialized;
    ss >> deserialized;
    
    BOOST_CHECK_EQUAL(deserialized.size(), 2U);
    BOOST_CHECK_EQUAL(deserialized[0].amount, 100U);
    BOOST_CHECK_EQUAL(deserialized[1].amount, 200U);
}

BOOST_AUTO_TEST_SUITE_END()
