// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/randomx_context.h>
#include <test/util/setup_common.h>
#include <uint256.h>

// Forward declare randomx functions we need - they're linked through opensy_node
struct randomx_vm;
extern "C" {
    void randomx_calculate_hash(randomx_vm *machine, const void *input, size_t inputSize, void *output);
    void randomx_destroy_vm(randomx_vm *machine);
}

#include <atomic>
#include <array>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>

/**
 * RandomXMiningContext Unit Tests
 *
 * These tests verify the mining-optimized RandomX context:
 * - Full dataset initialization (2GB mode)
 * - Per-thread VM creation
 * - Multi-threaded dataset initialization
 * - Concurrent VM usage safety
 * - Memory management and cleanup
 */

BOOST_FIXTURE_TEST_SUITE(randomx_mining_context_tests, BasicTestingSetup)

// Use simple test key hashes (same pattern as other randomx tests)
static const uint256 TEST_KEY1{"1111111111111111111111111111111111111111111111111111111111111111"};
static const uint256 TEST_KEY2{"2222222222222222222222222222222222222222222222222222222222222222"};

// =============================================================================
// BASIC FUNCTIONALITY TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(default_construction)
{
    // Test: Default construction creates uninitialized context
    RandomXMiningContext ctx;
    BOOST_CHECK(!ctx.IsInitialized());
    BOOST_CHECK(ctx.GetKeyBlockHash() == uint256());
}

BOOST_AUTO_TEST_CASE(initialization_with_key)
{
    // Test: Initialize with a key block hash
    // Note: This test may be slow (~10-30 seconds) due to dataset generation
    RandomXMiningContext ctx;
    
    // Single-threaded init for predictable test behavior
    bool result = ctx.Initialize(TEST_KEY1, 1);
    
    BOOST_CHECK_MESSAGE(result, "RandomXMiningContext initialization should succeed");
    BOOST_CHECK(ctx.IsInitialized());
    BOOST_CHECK(ctx.GetKeyBlockHash() == TEST_KEY1);
}

BOOST_AUTO_TEST_CASE(create_vm_without_init)
{
    // Test: CreateVM should return nullptr if not initialized
    RandomXMiningContext ctx;
    
    BOOST_CHECK(!ctx.IsInitialized());
    randomx_vm* vm = ctx.CreateVM();
    BOOST_CHECK(vm == nullptr);
}

BOOST_AUTO_TEST_CASE(create_vm_after_init)
{
    // Test: CreateVM should succeed after initialization
    RandomXMiningContext ctx;
    
    BOOST_REQUIRE(ctx.Initialize(TEST_KEY1, 1));
    
    randomx_vm* vm = ctx.CreateVM();
    BOOST_CHECK(vm != nullptr);
    
    if (vm) {
        randomx_destroy_vm(vm);
    }
}

BOOST_AUTO_TEST_CASE(vm_hash_calculation)
{
    // Test: VM from mining context can calculate hashes
    RandomXMiningContext ctx;
    
    BOOST_REQUIRE(ctx.Initialize(TEST_KEY1, 1));
    
    randomx_vm* vm = ctx.CreateVM();
    BOOST_REQUIRE(vm != nullptr);
    
    // Calculate a hash
    const char* input = "test input for RandomX hash calculation";
    std::array<unsigned char, 32> hash;
    randomx_calculate_hash(vm, input, strlen(input), hash.data());
    
    // Hash should not be all zeros
    bool allZeros = true;
    for (auto byte : hash) {
        if (byte != 0) {
            allZeros = false;
            break;
        }
    }
    BOOST_CHECK_MESSAGE(!allZeros, "RandomX hash should not be all zeros");
    
    randomx_destroy_vm(vm);
}

BOOST_AUTO_TEST_CASE(hash_determinism)
{
    // Test: Same input with same key produces same hash
    RandomXMiningContext ctx;
    
    BOOST_REQUIRE(ctx.Initialize(TEST_KEY1, 1));
    
    randomx_vm* vm1 = ctx.CreateVM();
    randomx_vm* vm2 = ctx.CreateVM();
    BOOST_REQUIRE(vm1 != nullptr && vm2 != nullptr);
    
    const char* input = "determinism test input";
    std::array<unsigned char, 32> hash1, hash2;
    
    randomx_calculate_hash(vm1, input, strlen(input), hash1.data());
    randomx_calculate_hash(vm2, input, strlen(input), hash2.data());
    
    BOOST_CHECK(hash1 == hash2);
    
    randomx_destroy_vm(vm1);
    randomx_destroy_vm(vm2);
}

// =============================================================================
// MULTI-THREADED TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(multi_thread_dataset_init)
{
    // Test: Multi-threaded dataset initialization
    RandomXMiningContext ctx;
    
    // Use 2 threads (safe for CI environments)
    unsigned int numThreads = 2;
    bool result = ctx.Initialize(TEST_KEY1, numThreads);
    
    BOOST_CHECK(result);
    BOOST_CHECK(ctx.IsInitialized());
}

BOOST_AUTO_TEST_CASE(concurrent_vm_creation)
{
    // Test: Multiple threads can create VMs concurrently
    RandomXMiningContext ctx;
    
    BOOST_REQUIRE(ctx.Initialize(TEST_KEY1, 1));
    
    const int numThreads = 4;
    std::vector<std::thread> threads;
    std::vector<randomx_vm*> vms(numThreads, nullptr);
    std::atomic<int> successCount{0};
    
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&ctx, &vms, &successCount, i]() {
            randomx_vm* vm = ctx.CreateVM();
            if (vm) {
                vms[i] = vm;
                ++successCount;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    BOOST_CHECK_EQUAL(successCount.load(), numThreads);
    
    // Cleanup
    for (auto vm : vms) {
        if (vm) randomx_destroy_vm(vm);
    }
}

BOOST_AUTO_TEST_CASE(concurrent_hash_calculation)
{
    // Test: Multiple threads can calculate hashes concurrently using their own VMs
    RandomXMiningContext ctx;
    
    BOOST_REQUIRE(ctx.Initialize(TEST_KEY1, 1));
    
    constexpr int numThreads = 4;
    constexpr int hashesPerThread = 10;
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};
    
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&ctx, &successCount, t]() {
            randomx_vm* vm = ctx.CreateVM();
            if (!vm) return;
            
            for (int i = 0; i < hashesPerThread; ++i) {
                std::string input = "thread " + std::to_string(t) + " hash " + std::to_string(i);
                std::array<unsigned char, 32> hash;
                randomx_calculate_hash(vm, input.c_str(), input.size(), hash.data());
                ++successCount;
            }
            
            randomx_destroy_vm(vm);
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    BOOST_CHECK_EQUAL(successCount.load(), numThreads * hashesPerThread);
}

// =============================================================================
// REINITIALIZATION TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(reinitialization_with_different_key)
{
    // Test: Context can be reinitialized with a different key
    RandomXMiningContext ctx;
    
    BOOST_REQUIRE(ctx.Initialize(TEST_KEY1, 1));
    BOOST_CHECK(ctx.GetKeyBlockHash() == TEST_KEY1);
    
    // Create a VM and calculate hash with first key
    randomx_vm* vm1 = ctx.CreateVM();
    BOOST_REQUIRE(vm1 != nullptr);
    
    const char* input = "reinit test";
    std::array<unsigned char, 32> hash1;
    randomx_calculate_hash(vm1, input, strlen(input), hash1.data());
    randomx_destroy_vm(vm1);
    
    // Reinitialize with different key
    BOOST_REQUIRE(ctx.Initialize(TEST_KEY2, 1));
    BOOST_CHECK(ctx.GetKeyBlockHash() == TEST_KEY2);
    
    // Calculate hash with second key - should be different
    randomx_vm* vm2 = ctx.CreateVM();
    BOOST_REQUIRE(vm2 != nullptr);
    
    std::array<unsigned char, 32> hash2;
    randomx_calculate_hash(vm2, input, strlen(input), hash2.data());
    randomx_destroy_vm(vm2);
    
    BOOST_CHECK_MESSAGE(hash1 != hash2, "Hashes with different keys should differ");
}

BOOST_AUTO_TEST_CASE(reinitialization_with_same_key)
{
    // Test: Reinitialization with same key produces same results
    RandomXMiningContext ctx;
    
    const char* input = "same key reinit test";
    
    // First initialization
    BOOST_REQUIRE(ctx.Initialize(TEST_KEY1, 1));
    randomx_vm* vm1 = ctx.CreateVM();
    BOOST_REQUIRE(vm1 != nullptr);
    std::array<unsigned char, 32> hash1;
    randomx_calculate_hash(vm1, input, strlen(input), hash1.data());
    randomx_destroy_vm(vm1);
    
    // Reinitialize with same key
    BOOST_REQUIRE(ctx.Initialize(TEST_KEY1, 1));
    randomx_vm* vm2 = ctx.CreateVM();
    BOOST_REQUIRE(vm2 != nullptr);
    std::array<unsigned char, 32> hash2;
    randomx_calculate_hash(vm2, input, strlen(input), hash2.data());
    randomx_destroy_vm(vm2);
    
    BOOST_CHECK(hash1 == hash2);
}

// =============================================================================
// EDGE CASE TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(zero_key_block_hash)
{
    // Test: Initialization with zero hash should still work (unusual but valid)
    RandomXMiningContext ctx;
    
    uint256 zeroHash; // Default is all zeros
    
    bool result = ctx.Initialize(zeroHash, 1);
    BOOST_CHECK(result);
    BOOST_CHECK(ctx.IsInitialized());
    BOOST_CHECK(ctx.GetKeyBlockHash() == zeroHash);
}

BOOST_AUTO_TEST_CASE(empty_input_hash)
{
    // Test: Hashing empty input should produce valid (non-zero) hash
    RandomXMiningContext ctx;
    
    BOOST_REQUIRE(ctx.Initialize(TEST_KEY1, 1));
    
    randomx_vm* vm = ctx.CreateVM();
    BOOST_REQUIRE(vm != nullptr);
    
    std::array<unsigned char, 32> hash;
    randomx_calculate_hash(vm, "", 0, hash.data());
    
    // Empty input should still produce a valid hash
    bool allZeros = true;
    for (auto byte : hash) {
        if (byte != 0) {
            allZeros = false;
            break;
        }
    }
    BOOST_CHECK_MESSAGE(!allZeros, "Empty input should produce non-zero hash");
    
    randomx_destroy_vm(vm);
}

BOOST_AUTO_TEST_CASE(large_input_hash)
{
    // Test: Hashing large input works correctly
    RandomXMiningContext ctx;
    
    BOOST_REQUIRE(ctx.Initialize(TEST_KEY1, 1));
    
    randomx_vm* vm = ctx.CreateVM();
    BOOST_REQUIRE(vm != nullptr);
    
    // Create 1MB input
    std::vector<unsigned char> largeInput(1024 * 1024);
    for (size_t i = 0; i < largeInput.size(); ++i) {
        largeInput[i] = static_cast<unsigned char>(i & 0xFF);
    }
    
    std::array<unsigned char, 32> hash;
    randomx_calculate_hash(vm, largeInput.data(), largeInput.size(), hash.data());
    
    bool allZeros = true;
    for (auto byte : hash) {
        if (byte != 0) {
            allZeros = false;
            break;
        }
    }
    BOOST_CHECK_MESSAGE(!allZeros, "Large input should produce valid hash");
    
    randomx_destroy_vm(vm);
}

// =============================================================================
// DESTRUCTOR AND CLEANUP TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(destructor_cleanup)
{
    // Test: Destructor properly cleans up resources (no leak check, but no crash)
    {
        RandomXMiningContext ctx;
        ctx.Initialize(TEST_KEY1, 1);
        
        // Create some VMs
        randomx_vm* vm1 = ctx.CreateVM();
        randomx_vm* vm2 = ctx.CreateVM();
        
        // Destroy VMs before context goes out of scope
        if (vm1) randomx_destroy_vm(vm1);
        if (vm2) randomx_destroy_vm(vm2);
    }
    // Context destructor called here - should not crash
    
    BOOST_CHECK(true); // If we get here, destructor worked
}

BOOST_AUTO_TEST_CASE(vm_outlives_partial_context_use)
{
    // Test: VMs created from context can be used independently
    // (though in practice they depend on the dataset)
    RandomXMiningContext ctx;
    
    BOOST_REQUIRE(ctx.Initialize(TEST_KEY1, 1));
    
    randomx_vm* vm = ctx.CreateVM();
    BOOST_REQUIRE(vm != nullptr);
    
    // Calculate hash while context is alive
    const char* input = "test";
    std::array<unsigned char, 32> hash;
    randomx_calculate_hash(vm, input, strlen(input), hash.data());
    
    // VM must be destroyed before context
    randomx_destroy_vm(vm);
    
    BOOST_CHECK(true);
}

// =============================================================================
// EPOCH-BASED VM INVALIDATION TESTS (Security Fix)
// =============================================================================

BOOST_AUTO_TEST_CASE(epoch_starts_at_zero)
{
    // Test: New context has epoch 0
    RandomXMiningContext ctx;
    BOOST_CHECK_EQUAL(ctx.GetDatasetEpoch(), 0);
}

BOOST_AUTO_TEST_CASE(epoch_unchanged_after_first_init)
{
    // Test: First initialization doesn't increment epoch (no prior dataset to free)
    RandomXMiningContext ctx;
    uint64_t epoch_before = ctx.GetDatasetEpoch();
    
    BOOST_REQUIRE(ctx.Initialize(TEST_KEY1, 1));
    
    // Epoch should still be 0 after first init (no prior dataset was freed)
    BOOST_CHECK_EQUAL(ctx.GetDatasetEpoch(), epoch_before);
}

BOOST_AUTO_TEST_CASE(epoch_increments_on_reinit)
{
    // Test: Epoch increments when reinitializing with different key
    RandomXMiningContext ctx;
    
    BOOST_REQUIRE(ctx.Initialize(TEST_KEY1, 1));
    uint64_t epoch_after_first = ctx.GetDatasetEpoch();
    
    // Reinitialize with different key - should increment epoch
    BOOST_REQUIRE(ctx.Initialize(TEST_KEY2, 1));
    uint64_t epoch_after_second = ctx.GetDatasetEpoch();
    
    BOOST_CHECK_GT(epoch_after_second, epoch_after_first);
}

BOOST_AUTO_TEST_CASE(epoch_unchanged_same_key)
{
    // Test: Epoch unchanged when reinitializing with same key (optimization)
    RandomXMiningContext ctx;
    
    BOOST_REQUIRE(ctx.Initialize(TEST_KEY1, 1));
    uint64_t epoch1 = ctx.GetDatasetEpoch();
    
    // Reinitialize with SAME key - should NOT increment epoch (no-op)
    BOOST_REQUIRE(ctx.Initialize(TEST_KEY1, 1));
    uint64_t epoch2 = ctx.GetDatasetEpoch();
    
    BOOST_CHECK_EQUAL(epoch1, epoch2);
}

BOOST_AUTO_TEST_CASE(epoch_detects_stale_vm)
{
    // Test: Mining threads can detect stale VMs via epoch check
    // This simulates the key rotation scenario that caused crashes
    RandomXMiningContext ctx;
    
    BOOST_REQUIRE(ctx.Initialize(TEST_KEY1, 1));
    
    // Capture epoch at "mining start"
    uint64_t mining_epoch = ctx.GetDatasetEpoch();
    
    // Create VM (simulating mining thread startup)
    randomx_vm* vm = ctx.CreateVM();
    BOOST_REQUIRE(vm != nullptr);
    
    // Simulate key rotation occurring during mining
    BOOST_REQUIRE(ctx.Initialize(TEST_KEY2, 1));
    
    // Mining thread should detect epoch mismatch
    BOOST_CHECK_NE(ctx.GetDatasetEpoch(), mining_epoch);
    
    // Cleanup - in real code, thread would abort before this if epoch changed
    randomx_destroy_vm(vm);
}

BOOST_AUTO_TEST_CASE(concurrent_epoch_check_safety)
{
    // Test: Concurrent epoch checks are safe (lock-free reads)
    RandomXMiningContext ctx;
    
    BOOST_REQUIRE(ctx.Initialize(TEST_KEY1, 1));
    
    std::atomic<bool> stop{false};
    std::atomic<int> epoch_checks{0};
    std::atomic<int> epoch_changes_detected{0};
    
    uint64_t initial_epoch = ctx.GetDatasetEpoch();
    
    // Start threads that continuously check epoch
    std::vector<std::thread> checkers;
    for (int i = 0; i < 4; ++i) {
        checkers.emplace_back([&ctx, &stop, &epoch_checks, &epoch_changes_detected, initial_epoch]() {
            while (!stop.load(std::memory_order_relaxed)) {
                uint64_t current = ctx.GetDatasetEpoch();
                epoch_checks.fetch_add(1, std::memory_order_relaxed);
                if (current != initial_epoch) {
                    epoch_changes_detected.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    
    // Let checkers run briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Trigger key rotation
    ctx.Initialize(TEST_KEY2, 1);
    
    // Let checkers detect it
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    stop.store(true, std::memory_order_relaxed);
    for (auto& t : checkers) t.join();
    
    // Should have done many checks and detected the change
    BOOST_CHECK_GT(epoch_checks.load(), 0);
    BOOST_CHECK_GT(epoch_changes_detected.load(), 0);
}

BOOST_AUTO_TEST_SUITE_END()
