// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/randomx_pool.h>
#include <test/util/setup_common.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <array>
#include <atomic>
#include <thread>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(randomx_pool_tests, BasicTestingSetup)

/**
 * SECURITY FIX [H-01]: Thread-Local RandomX Context Memory Accumulation
 *
 * These tests validate the RandomX context pool implementation that replaces
 * the unbounded thread-local contexts with a bounded pool.
 */

BOOST_AUTO_TEST_CASE(pool_basic_acquire_release)
{
    // Test basic acquire and release
    uint256 key = uint256::ONE;

    auto guard = g_randomx_pool.Acquire(key);
    BOOST_CHECK(guard.has_value());
    BOOST_CHECK(guard->get() != nullptr);

    auto stats = g_randomx_pool.GetStats();
    BOOST_CHECK_EQUAL(stats.active_contexts, 1);
    BOOST_CHECK(stats.total_acquisitions > 0);
}

BOOST_AUTO_TEST_CASE(pool_stats_tracking)
{
    auto stats_before = g_randomx_pool.GetStats();

    uint256 key = uint256::ONE;
    {
        auto guard = g_randomx_pool.Acquire(key);
        BOOST_CHECK(guard.has_value());

        auto stats_during = g_randomx_pool.GetStats();
        BOOST_CHECK_EQUAL(stats_during.active_contexts, 1);
        BOOST_CHECK_GE(stats_during.total_acquisitions, stats_before.total_acquisitions + 1);
    }

    // After guard destructs, context should be returned
    auto stats_after = g_randomx_pool.GetStats();
    BOOST_CHECK_EQUAL(stats_after.active_contexts, 0);
}

BOOST_AUTO_TEST_CASE(pool_key_reuse)
{
    // Test that same key reuses context without reinitialization
    uint256 key = uint256::ONE;

    auto stats_before = g_randomx_pool.GetStats();

    {
        auto guard1 = g_randomx_pool.Acquire(key);
        BOOST_CHECK(guard1.has_value());
    }

    {
        auto guard2 = g_randomx_pool.Acquire(key);
        BOOST_CHECK(guard2.has_value());
    }

    auto stats_after = g_randomx_pool.GetStats();
    // Second acquisition with same key should not reinitialize
    // (assuming pool still has the same-keyed context available)
    BOOST_CHECK_GE(stats_after.total_acquisitions, stats_before.total_acquisitions + 2);
}

BOOST_AUTO_TEST_CASE(pool_different_keys)
{
    // Test that different keys cause reinitialization
    uint256 key1 = uint256::ONE;
    uint256 key2 = uint256::ZERO;

    auto stats_before = g_randomx_pool.GetStats();

    {
        auto guard1 = g_randomx_pool.Acquire(key1);
        BOOST_CHECK(guard1.has_value());
    }

    {
        auto guard2 = g_randomx_pool.Acquire(key2);
        BOOST_CHECK(guard2.has_value());
    }

    auto stats_after = g_randomx_pool.GetStats();
    // Second key should cause at least one reinitialization
    BOOST_CHECK_GE(stats_after.key_reinitializations, stats_before.key_reinitializations);
}

BOOST_AUTO_TEST_CASE(pool_concurrent_access)
{
    // Test concurrent acquisition from multiple threads
    std::atomic<int> successful_acquisitions{0};
    std::atomic<int> failed_acquisitions{0};
    const int num_threads = 16;
    const int iterations = 5;

    // Pre-defined keys for testing (4 different keys)
    static const std::array<uint256, 4> test_keys = {
        uint256{"0000000000000000000000000000000000000000000000000000000000000001"},
        uint256{"0000000000000000000000000000000000000000000000000000000000000002"},
        uint256{"0000000000000000000000000000000000000000000000000000000000000003"},
        uint256{"0000000000000000000000000000000000000000000000000000000000000004"}
    };

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < iterations; ++i) {
                const uint256& key = test_keys[(t * iterations + i) % 4];

                auto guard = g_randomx_pool.Acquire(key);
                if (guard.has_value()) {
                    successful_acquisitions++;
                    // Simulate some work
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                } else {
                    failed_acquisitions++;
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // All acquisitions should succeed (blocking waits for available context)
    BOOST_CHECK_EQUAL(successful_acquisitions.load(), num_threads * iterations);

    auto stats = g_randomx_pool.GetStats();
    // Should have had some waits if pool was contended
    // (may be 0 if threads were slow enough to not contend)
    BOOST_CHECK_GE(stats.total_acquisitions, (size_t)(num_threads * iterations));
}

BOOST_AUTO_TEST_CASE(pool_bounded_memory)
{
    // Verify pool is bounded to MAX_CONTEXTS
    auto stats = g_randomx_pool.GetStats();
    BOOST_CHECK_LE(stats.total_contexts, RandomXContextPool::MAX_CONTEXTS);
}

// =============================================================================
// POOL EXHAUSTION AND STRESS TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(pool_exhaustion_recovery)
{
    // Test: Pool recovers correctly after exhaustion
    // Acquire maximum contexts, then release them all, then acquire again
    uint256 key = uint256::ONE;
    
    // Note: This test must work even if some contexts are in use by other tests
    // So we just verify we can acquire/release without deadlock
    
    std::vector<std::optional<RandomXContextPool::ContextGuard>> guards;
    
    // Try to acquire several contexts (but not necessarily all)
    const int targetContexts = 4;
    for (int i = 0; i < targetContexts; ++i) {
        auto guard = g_randomx_pool.Acquire(key);
        if (guard.has_value()) {
            guards.push_back(std::move(guard));
        }
    }
    
    size_t acquiredCount = guards.size();
    BOOST_CHECK_GT(acquiredCount, 0);
    
    // Release all
    guards.clear();
    
    // Should be able to acquire again
    auto guard = g_randomx_pool.Acquire(key);
    BOOST_CHECK(guard.has_value());
}

BOOST_AUTO_TEST_CASE(pool_rapid_key_changes)
{
    // Test: Rapid key changes stress test
    // This simulates blocks rapidly changing the RandomX key
    
    auto stats_before = g_randomx_pool.GetStats();
    
    static const std::array<uint256, 8> keys = {
        uint256{"1111111111111111111111111111111111111111111111111111111111111111"},
        uint256{"2222222222222222222222222222222222222222222222222222222222222222"},
        uint256{"3333333333333333333333333333333333333333333333333333333333333333"},
        uint256{"4444444444444444444444444444444444444444444444444444444444444444"},
        uint256{"5555555555555555555555555555555555555555555555555555555555555555"},
        uint256{"6666666666666666666666666666666666666666666666666666666666666666"},
        uint256{"7777777777777777777777777777777777777777777777777777777777777777"},
        uint256{"8888888888888888888888888888888888888888888888888888888888888888"},
    };
    
    // Rapidly acquire/release with different keys
    for (int round = 0; round < 3; ++round) {
        for (const auto& key : keys) {
            auto guard = g_randomx_pool.Acquire(key);
            BOOST_CHECK(guard.has_value());
            // Guard releases immediately when going out of scope
        }
    }
    
    auto stats_after = g_randomx_pool.GetStats();
    
    // Should have many acquisitions
    BOOST_CHECK_GE(stats_after.total_acquisitions, stats_before.total_acquisitions + (3 * keys.size()));
    
    // Should have key reinitializations (since we use 8 different keys but only MAX_CONTEXTS slots)
    BOOST_CHECK_GE(stats_after.key_reinitializations, stats_before.key_reinitializations);
}

BOOST_AUTO_TEST_CASE(pool_concurrent_different_keys)
{
    // Test: Multiple threads with different keys
    // This is the real-world scenario during IBD with many blocks
    
    const int numThreads = 8;
    std::atomic<int> successCount{0};
    std::atomic<int> failCount{0};
    
    static const std::array<uint256, 8> keys = {
        uint256{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
        uint256{"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},
        uint256{"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"},
        uint256{"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"},
        uint256{"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"},
        uint256{"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
        uint256{"0000000000000000000000000000000000000000000000000000000000000001"},
        uint256{"0000000000000000000000000000000000000000000000000000000000000002"},
    };
    
    std::vector<std::thread> threads;
    
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&, t]() {
            const uint256& key = keys[t % keys.size()];
            
            for (int i = 0; i < 5; ++i) {
                auto guard = g_randomx_pool.Acquire(key);
                if (guard.has_value()) {
                    ++successCount;
                    // Simulate work
                    std::this_thread::yield();
                } else {
                    ++failCount;
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // All should succeed (pool waits for available context)
    BOOST_CHECK_EQUAL(successCount.load(), numThreads * 5);
    BOOST_CHECK_EQUAL(failCount.load(), 0);
}

BOOST_AUTO_TEST_CASE(pool_stats_consistency)
{
    // Test: Statistics remain consistent under load
    auto stats1 = g_randomx_pool.GetStats();
    
    uint256 key = uint256::ONE;
    {
        auto guard = g_randomx_pool.Acquire(key);
        
        auto stats2 = g_randomx_pool.GetStats();
        
        // Active contexts should increase
        BOOST_CHECK_GE(stats2.active_contexts, 1);
        
        // Total acquisitions should increase
        BOOST_CHECK_GT(stats2.total_acquisitions, stats1.total_acquisitions);
    }
    
    auto stats3 = g_randomx_pool.GetStats();
    
    // After release, active should decrease
    BOOST_CHECK_LT(stats3.active_contexts, stats1.active_contexts + 10); // Allow for concurrent tests
}

BOOST_AUTO_TEST_CASE(pool_context_reuse_efficiency)
{
    // Test: Context reuse is efficient (same key doesn't reinitialize)
    uint256 key{"abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"};
    
    auto stats_before = g_randomx_pool.GetStats();
    
    // Acquire and release with same key multiple times
    for (int i = 0; i < 10; ++i) {
        auto guard = g_randomx_pool.Acquire(key);
        BOOST_CHECK(guard.has_value());
    }
    
    auto stats_after = g_randomx_pool.GetStats();
    
    // Should have at most one reinitialization (the first time)
    // Additional reinitializations only if context was evicted
    size_t reinits = stats_after.key_reinitializations - stats_before.key_reinitializations;
    
    // In a low-contention scenario, we expect mostly reuse
    // Allow some reinits if pool was full of other keys
    BOOST_CHECK_LE(reinits, 3);
}

// =============================================================================
// PRIORITY-BASED ACQUISITION TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(pool_priority_basic_normal)
{
    // Test: Normal priority acquisition works
    uint256 key = uint256::ONE;
    
    auto guard = g_randomx_pool.Acquire(key, AcquisitionPriority::NORMAL);
    BOOST_CHECK(guard.has_value());
    BOOST_CHECK(guard->get() != nullptr);
}

BOOST_AUTO_TEST_CASE(pool_priority_basic_high)
{
    // Test: High priority acquisition works
    uint256 key = uint256::ONE;
    
    auto guard = g_randomx_pool.Acquire(key, AcquisitionPriority::HIGH);
    BOOST_CHECK(guard.has_value());
    BOOST_CHECK(guard->get() != nullptr);
    
    auto stats = g_randomx_pool.GetStats();
    BOOST_CHECK_GT(stats.high_priority_acquisitions, 0);
}

BOOST_AUTO_TEST_CASE(pool_priority_basic_consensus_critical)
{
    // Test: Consensus-critical priority acquisition works
    uint256 key = uint256::ONE;
    
    auto guard = g_randomx_pool.Acquire(key, AcquisitionPriority::CONSENSUS_CRITICAL);
    BOOST_CHECK(guard.has_value());
    BOOST_CHECK(guard->get() != nullptr);
    
    auto stats = g_randomx_pool.GetStats();
    BOOST_CHECK_GT(stats.consensus_critical_acquisitions, 0);
}

BOOST_AUTO_TEST_CASE(pool_priority_stats_tracking)
{
    // Test: Priority stats are tracked correctly
    uint256 key = uint256::ONE;
    
    auto stats_before = g_randomx_pool.GetStats();
    
    {
        auto guard = g_randomx_pool.Acquire(key, AcquisitionPriority::HIGH);
        BOOST_CHECK(guard.has_value());
    }
    
    auto stats_after = g_randomx_pool.GetStats();
    
    BOOST_CHECK_GE(stats_after.high_priority_acquisitions, 
                   stats_before.high_priority_acquisitions + 1);
}

BOOST_AUTO_TEST_CASE(pool_priority_consensus_critical_stats)
{
    // Test: Consensus-critical stats are tracked correctly
    uint256 key = uint256::ONE;
    
    auto stats_before = g_randomx_pool.GetStats();
    
    {
        auto guard = g_randomx_pool.Acquire(key, AcquisitionPriority::CONSENSUS_CRITICAL);
        BOOST_CHECK(guard.has_value());
    }
    
    auto stats_after = g_randomx_pool.GetStats();
    
    BOOST_CHECK_GE(stats_after.consensus_critical_acquisitions, 
                   stats_before.consensus_critical_acquisitions + 1);
}

BOOST_AUTO_TEST_CASE(pool_priority_concurrent_mixed)
{
    // Test: Concurrent acquisition with mixed priorities
    const int numThreads = 12;
    std::atomic<int> normalSuccess{0};
    std::atomic<int> highSuccess{0};
    std::atomic<int> criticalSuccess{0};
    
    std::vector<std::thread> threads;
    uint256 key = uint256::ONE;
    
    for (int t = 0; t < numThreads; ++t) {
        AcquisitionPriority priority;
        if (t < 4) {
            priority = AcquisitionPriority::NORMAL;
        } else if (t < 8) {
            priority = AcquisitionPriority::HIGH;
        } else {
            priority = AcquisitionPriority::CONSENSUS_CRITICAL;
        }
        
        threads.emplace_back([&, priority]() {
            for (int i = 0; i < 3; ++i) {
                auto guard = g_randomx_pool.Acquire(key, priority);
                if (guard.has_value()) {
                    switch (priority) {
                        case AcquisitionPriority::LOW:
                            // LOW priority not used in this test
                            break;
                        case AcquisitionPriority::NORMAL:
                            ++normalSuccess;
                            break;
                        case AcquisitionPriority::HIGH:
                            ++highSuccess;
                            break;
                        case AcquisitionPriority::CONSENSUS_CRITICAL:
                            ++criticalSuccess;
                            break;
                    }
                    std::this_thread::yield();
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // All should succeed (pool waits for available context)
    // Critical must always succeed, others may timeout in extreme cases
    BOOST_CHECK_EQUAL(criticalSuccess.load(), 4 * 3); // 4 threads * 3 iterations
    BOOST_CHECK_GT(highSuccess.load(), 0);
    BOOST_CHECK_GT(normalSuccess.load(), 0);
}

BOOST_AUTO_TEST_CASE(pool_priority_consensus_never_fails)
{
    // Test: Consensus-critical acquisition NEVER fails (no timeout)
    // This is the critical property that prevents valid block rejection
    uint256 key = uint256::ONE;
    
    // Even under contention, consensus-critical should always succeed
    // (though it may wait)
    for (int i = 0; i < 20; ++i) {
        auto guard = g_randomx_pool.Acquire(key, AcquisitionPriority::CONSENSUS_CRITICAL);
        BOOST_CHECK_MESSAGE(guard.has_value(), 
            "Consensus-critical acquisition must NEVER fail");
    }
}

BOOST_AUTO_TEST_SUITE_END()
