// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/transaction.h>
#include <script/script.h>
#include <script/src20.h>
#include <test/util/setup_common.h>
#include <tokens/tokendb.h>
#include <tokens/tokenvalidation.h>
#include <uint256.h>
#include <util/fs.h>

#include <boost/test/unit_test.hpp>

#include <atomic>
#include <thread>
#include <vector>

/**
 * GAP-10: Concurrent ProcessBlock calls
 *
 * These tests verify that TokenDB handles concurrent operations correctly
 * without data races or corruption. Should be run with TSAN (Thread Sanitizer)
 * for full verification.
 *
 * Key scenarios:
 * - Multiple ProcessBlock calls from different threads
 * - Concurrent reads and writes
 * - Cache invalidation under contention
 */

namespace {

// Create a minimal block for testing
CBlock CreateTestBlock(int height, int nonce = 0)
{
    CBlock block;
    block.nVersion = 1;
    block.hashPrevBlock = uint256::ONE;
    block.hashMerkleRoot = uint256::ZERO;
    block.nTime = 1234567890 + height;
    block.nBits = 0x1d00ffff;
    block.nNonce = nonce;
    
    // Add coinbase
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << height << 0;
    coinbase.vout.resize(1);
    coinbase.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 
                                              << std::vector<unsigned char>(20, 0xAB)
                                              << OP_EQUALVERIFY << OP_CHECKSIG;
    coinbase.vout[0].nValue = 10000 * COIN;
    
    block.vtx.push_back(MakeTransactionRef(coinbase));
    
    return block;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(token_concurrent_tests, BasicTestingSetup)

// =============================================================================
// BASIC CONCURRENCY SAFETY
// =============================================================================

BOOST_AUTO_TEST_CASE(concurrent_process_block_no_crash)
{
    // Test: Multiple threads calling ProcessBlock don't crash
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_concurrent";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    std::atomic<bool> stop{false};
    std::atomic<int> blocks_processed{0};
    std::vector<std::thread> threads;
    
    // Multiple threads processing blocks
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&, i]() {
            int local_count = 0;
            int height = i * 1000;
            
            while (!stop.load() && local_count < 100) {
                CBlock block = CreateTestBlock(height + local_count, i);
                db.ProcessBlock(block, height + local_count);
                local_count++;
            }
            
            blocks_processed += local_count;
        });
    }
    
    // Run for a short time
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stop = true;
    
    for (auto& t : threads) {
        t.join();
    }
    
    BOOST_TEST_MESSAGE("Processed " << blocks_processed.load() << " blocks concurrently");
    BOOST_CHECK_GT(blocks_processed.load(), 0);
    
    // Cleanup
    fs::remove_all(test_path);
}

// =============================================================================
// CONCURRENT READS AND WRITES
// =============================================================================

BOOST_AUTO_TEST_CASE(concurrent_read_write_no_race)
{
    // Test: Concurrent reads and writes don't cause data races
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_rw";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    std::atomic<bool> stop{false};
    std::atomic<int> read_count{0};
    std::atomic<int> write_count{0};
    
    // Writer thread
    std::thread writer([&]() {
        int height = 0;
        while (!stop.load()) {
            CBlock block = CreateTestBlock(height++);
            db.ProcessBlock(block, height);
            write_count++;
        }
    });
    
    // Reader threads
    std::vector<std::thread> readers;
    for (int i = 0; i < 3; ++i) {
        readers.emplace_back([&]() {
            while (!stop.load()) {
                // Read operations
                db.GetTokenCount();
                db.TickerExists("TEST");
                db.TokenExists(src20::TokenId(uint256::ONE));
                read_count++;
            }
        });
    }
    
    // Run briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stop = true;
    
    writer.join();
    for (auto& t : readers) {
        t.join();
    }
    
    BOOST_TEST_MESSAGE("Writes: " << write_count.load() << ", Reads: " << read_count.load());
    BOOST_CHECK(true); // Passed if no crash/deadlock
    
    // Cleanup
    fs::remove_all(test_path);
}

// =============================================================================
// CACHE INVALIDATION UNDER CONTENTION
// =============================================================================

BOOST_AUTO_TEST_CASE(cache_invalidation_thread_safe)
{
    // Test: Cache operations are thread-safe
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_cache";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    std::atomic<bool> stop{false};
    std::atomic<int> cache_ops{0};
    
    // Multiple threads hitting cache
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            src20::TokenId token_id(uint256::ONE);
            while (!stop.load()) {
                // These trigger cache operations
                db.GetTokenInfo(token_id);
                db.TokenExists(token_id);
                db.GetTokenCount();
                cache_ops++;
            }
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop = true;
    
    for (auto& t : threads) {
        t.join();
    }
    
    BOOST_TEST_MESSAGE("Cache operations: " << cache_ops.load());
    BOOST_CHECK(true);
    
    // Cleanup
    fs::remove_all(test_path);
}

// =============================================================================
// MEMPOOL STATE CONCURRENCY
// =============================================================================

BOOST_AUTO_TEST_CASE(mempool_state_concurrent_access)
{
    // Test: MempoolTokenState handles concurrent access
    
    tokens::MempoolTokenState state;
    
    std::atomic<bool> stop{false};
    std::atomic<int> ops{0};
    
    std::vector<std::thread> threads;
    
    // Thread checking tickers
    threads.emplace_back([&]() {
        while (!stop.load()) {
            state.IsTickerPending("TEST");
            state.IsTickerPending("COIN");
            ops++;
        }
    });
    
    // Thread clearing state
    threads.emplace_back([&]() {
        while (!stop.load()) {
            state.Clear();
            ops++;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
    
    // Thread removing transactions
    threads.emplace_back([&]() {
        int i = 0;
        while (!stop.load()) {
            uint256 fake_txid;
            std::fill(fake_txid.begin(), fake_txid.end(), static_cast<unsigned char>(i++ % 256));
            state.RemoveTransaction(fake_txid);
            ops++;
        }
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop = true;
    
    for (auto& t : threads) {
        t.join();
    }
    
    BOOST_TEST_MESSAGE("Mempool ops: " << ops.load());
    BOOST_CHECK(true);
}

// =============================================================================
// DISCONNECT BLOCK CONCURRENCY
// =============================================================================

BOOST_AUTO_TEST_CASE(concurrent_disconnect_block)
{
    // Test: Concurrent DisconnectBlock calls don't cause issues
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_disconnect_conc";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    std::atomic<int> disconnect_count{0};
    std::vector<std::thread> threads;
    
    // Multiple threads disconnecting (should be safe even if no blocks to disconnect)
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < 50; ++j) {
                CBlock block = CreateTestBlock(i * 100 + j);
                db.DisconnectBlock(block, i * 100 + j);
                disconnect_count++;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    BOOST_TEST_MESSAGE("Disconnects: " << disconnect_count.load());
    BOOST_CHECK_EQUAL(disconnect_count.load(), 200); // 4 threads * 50 each
    
    // Cleanup
    fs::remove_all(test_path);
}

// =============================================================================
// LOCK ORDERING (DEADLOCK PREVENTION)
// =============================================================================

BOOST_AUTO_TEST_CASE(no_deadlock_under_load)
{
    // Test: No deadlock when multiple operations contend for locks
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_deadlock";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    std::atomic<bool> stop{false};
    std::atomic<bool> deadlock_detected{false};
    
    std::vector<std::thread> threads;
    
    // Mix of operations that acquire different locks
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&, i]() {
            auto start = std::chrono::steady_clock::now();
            int height = i * 100;
            
            while (!stop.load()) {
                // Check for deadlock (operation taking too long)
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - start).count() > 5) {
                    deadlock_detected = true;
                    break;
                }
                
                // Various operations
                CBlock block = CreateTestBlock(height++);
                db.ProcessBlock(block, height);
                db.GetTokenCount();
                db.GetTokenInfo(src20::TokenId(uint256::ONE));
                db.DisconnectBlock(block, height);
            }
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stop = true;
    
    for (auto& t : threads) {
        t.join();
    }
    
    BOOST_CHECK_MESSAGE(!deadlock_detected.load(), "Deadlock detected!");
    
    // Cleanup
    fs::remove_all(test_path);
}

// =============================================================================
// TOKEN COUNT ATOMICITY
// =============================================================================

BOOST_AUTO_TEST_CASE(token_count_atomic_updates)
{
    // Test: Token count is atomically updated
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_atomic";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    // Get initial count
    size_t initial_count = db.GetTokenCount();
    
    // Multiple threads reading count shouldn't cause issues
    std::atomic<bool> stop{false};
    std::atomic<int> reads{0};
    
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            while (!stop.load()) {
                size_t count = db.GetTokenCount();
                // Count should be consistent
                BOOST_CHECK_GE(count, initial_count);
                reads++;
            }
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop = true;
    
    for (auto& t : threads) {
        t.join();
    }
    
    BOOST_TEST_MESSAGE("Atomic reads: " << reads.load());
    BOOST_CHECK_GT(reads.load(), 0);
    
    // Cleanup
    fs::remove_all(test_path);
}

BOOST_AUTO_TEST_SUITE_END()
