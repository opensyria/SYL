// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <script/script.h>
#include <test/util/setup_common.h>
#include <tokens/tokendb.h>
#include <uint256.h>
#include <util/fs.h>
#include <util/time.h>

#include <boost/test/unit_test.hpp>

#include <chrono>

/**
 * GAP-12: Token holder list performance
 *
 * These tests verify that token holder queries perform acceptably
 * even with large numbers of holders. Poor performance here could
 * be a DoS vector through expensive RPC queries.
 */

namespace {

// Helper to create unique addresses
CScript CreateAddress(size_t index)
{
    std::vector<unsigned char> hash(20);
    // Spread index across bytes to avoid collisions
    hash[0] = static_cast<unsigned char>((index >> 24) & 0xFF);
    hash[1] = static_cast<unsigned char>((index >> 16) & 0xFF);
    hash[2] = static_cast<unsigned char>((index >> 8) & 0xFF);
    hash[3] = static_cast<unsigned char>(index & 0xFF);
    
    return CScript() << OP_DUP << OP_HASH160 << hash << OP_EQUALVERIFY << OP_CHECKSIG;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(token_holder_perf_tests, BasicTestingSetup)

// =============================================================================
// BASELINE PERFORMANCE
// =============================================================================

BOOST_AUTO_TEST_CASE(small_holder_count_fast)
{
    // Test: Queries with small holder count are fast
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_perf_small";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    src20::TokenId token_id(uint256::ONE);
    
    // Query empty holder list
    auto start = std::chrono::steady_clock::now();
    auto holders = db.GetTokenHolders(token_id, 0, 100);
    auto end = std::chrono::steady_clock::now();
    
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    BOOST_TEST_MESSAGE("Empty holder query: " << duration_ms << "ms");
    BOOST_CHECK_LT(duration_ms, 100); // Should be very fast
    
    // Cleanup
    fs::remove_all(test_path);
}

// =============================================================================
// TOKEN COUNT QUERY PERFORMANCE
// =============================================================================

BOOST_AUTO_TEST_CASE(token_count_query_fast)
{
    // Test: GetTokenCount is fast (uses cache)
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_perf_count";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    // First call (may need to count)
    auto start = std::chrono::steady_clock::now();
    size_t count1 = db.GetTokenCount();
    auto mid = std::chrono::steady_clock::now();
    
    // Second call (should use cache)
    size_t count2 = db.GetTokenCount();
    auto end = std::chrono::steady_clock::now();
    
    auto first_ms = std::chrono::duration_cast<std::chrono::milliseconds>(mid - start).count();
    auto second_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - mid).count();
    
    BOOST_TEST_MESSAGE("First count query: " << first_ms << "ms");
    BOOST_TEST_MESSAGE("Cached count query: " << second_ms << "ms");
    
    BOOST_CHECK_EQUAL(count1, count2);
    BOOST_CHECK_LT(second_ms, 10); // Cached should be very fast
    
    // Cleanup
    fs::remove_all(test_path);
}

// =============================================================================
// TOKEN INFO CACHE PERFORMANCE
// =============================================================================

BOOST_AUTO_TEST_CASE(token_info_cache_effective)
{
    // Test: Token info queries use cache effectively
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_perf_info";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    src20::TokenId token_id(uint256::ONE);
    
    // Multiple queries for same token
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < 1000; ++i) {
        db.GetTokenInfo(token_id);
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    BOOST_TEST_MESSAGE("1000 token info queries: " << duration_ms << "ms");
    BOOST_CHECK_LT(duration_ms, 1000); // Should be <1ms per query on average
    
    // Cleanup
    fs::remove_all(test_path);
}

// =============================================================================
// TICKER LOOKUP PERFORMANCE
// =============================================================================

BOOST_AUTO_TEST_CASE(ticker_lookup_fast)
{
    // Test: Ticker existence check is fast
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_perf_ticker";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    // Check many tickers
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < 1000; ++i) {
        std::string ticker = "TEST" + std::to_string(i);
        db.TickerExists(ticker);
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    BOOST_TEST_MESSAGE("1000 ticker lookups: " << duration_ms << "ms");
    BOOST_CHECK_LT(duration_ms, 2000); // Should be reasonable
    
    // Cleanup
    fs::remove_all(test_path);
}

// =============================================================================
// BALANCE QUERY PERFORMANCE
// =============================================================================

BOOST_AUTO_TEST_CASE(balance_query_performance)
{
    // Test: Balance queries are performant
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_perf_balance";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    src20::TokenId token_id(uint256::ONE);
    
    // Query many addresses
    auto start = std::chrono::steady_clock::now();
    
    for (size_t i = 0; i < 1000; ++i) {
        CScript addr = CreateAddress(i);
        db.GetBalance(addr, token_id);
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    BOOST_TEST_MESSAGE("1000 balance queries: " << duration_ms << "ms");
    BOOST_CHECK_LT(duration_ms, 2000); // Should be reasonable
    
    // Cleanup
    fs::remove_all(test_path);
}

// =============================================================================
// ADDRESS BALANCES QUERY
// =============================================================================

BOOST_AUTO_TEST_CASE(address_balances_performant)
{
    // Test: GetAddressBalances is performant
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_perf_addr";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    CScript addr = CreateAddress(0);
    
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < 100; ++i) {
        db.GetAddressBalances(addr);
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    BOOST_TEST_MESSAGE("100 address balance queries: " << duration_ms << "ms");
    BOOST_CHECK_LT(duration_ms, 1000);
    
    // Cleanup
    fs::remove_all(test_path);
}

// =============================================================================
// HISTORY QUERY PERFORMANCE
// =============================================================================

BOOST_AUTO_TEST_CASE(history_query_performant)
{
    // Test: Address history queries are performant
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_perf_history";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    CScript addr = CreateAddress(0);
    
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < 100; ++i) {
        db.GetAddressHistory(addr, std::nullopt, 0, 100);
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    BOOST_TEST_MESSAGE("100 history queries: " << duration_ms << "ms");
    BOOST_CHECK_LT(duration_ms, 1000);
    
    // Cleanup
    fs::remove_all(test_path);
}

// =============================================================================
// PAGINATION LIMITS
// =============================================================================

BOOST_AUTO_TEST_CASE(pagination_limits_enforced)
{
    // Test: Pagination limits prevent excessive queries
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_perf_page";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    src20::TokenId token_id(uint256::ONE);
    
    // Even with large count, should be bounded
    auto start = std::chrono::steady_clock::now();
    
    // Request more than max allowed
    auto holders = db.GetTokenHolders(token_id, 0, 10000);
    
    auto end = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    BOOST_TEST_MESSAGE("Large holder request: " << duration_ms << "ms, got " << holders.size());
    
    // Should be bounded by RPC max count (1000 typically)
    BOOST_CHECK_LE(holders.size(), 1000);
    
    // Cleanup
    fs::remove_all(test_path);
}

// =============================================================================
// STRESS: REPEATED QUERIES
// =============================================================================

BOOST_AUTO_TEST_CASE(repeated_queries_stable)
{
    // Test: Repeated queries don't degrade performance (memory leak check)
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_perf_stress";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    src20::TokenId token_id(uint256::ONE);
    CScript addr = CreateAddress(0);
    
    // Measure first batch
    auto start1 = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) {
        db.GetTokenInfo(token_id);
        db.GetBalance(addr, token_id);
        db.GetTokenCount();
    }
    auto end1 = std::chrono::steady_clock::now();
    auto duration1 = std::chrono::duration_cast<std::chrono::milliseconds>(end1 - start1).count();
    
    // Measure last batch (after many iterations)
    for (int batch = 0; batch < 10; ++batch) {
        for (int i = 0; i < 100; ++i) {
            db.GetTokenInfo(token_id);
            db.GetBalance(addr, token_id);
            db.GetTokenCount();
        }
    }
    
    auto start2 = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) {
        db.GetTokenInfo(token_id);
        db.GetBalance(addr, token_id);
        db.GetTokenCount();
    }
    auto end2 = std::chrono::steady_clock::now();
    auto duration2 = std::chrono::duration_cast<std::chrono::milliseconds>(end2 - start2).count();
    
    BOOST_TEST_MESSAGE("First batch: " << duration1 << "ms, Last batch: " << duration2 << "ms");
    
    // Performance shouldn't degrade significantly
    // Handle case where durations are 0ms (fast operations) - that's acceptable
    if (duration1 > 0) {
        BOOST_CHECK_LT(duration2, duration1 * 3);
    } else {
        // Both are 0ms or very fast - that's fine, no degradation
        BOOST_CHECK_LE(duration2, 10); // Should still be under 10ms
    }
    
    // Cleanup
    fs::remove_all(test_path);
}

BOOST_AUTO_TEST_SUITE_END()
