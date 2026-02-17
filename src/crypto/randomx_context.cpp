// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/randomx_context.h>
#include <logging.h>
#include <util/check.h>

#include <randomx.h>

#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

std::unique_ptr<RandomXContext> g_randomx_context;

void RandomXContext::Cleanup()
{
    AssertLockHeld(m_mutex);

    if (m_vm) {
        randomx_destroy_vm(m_vm);
        m_vm = nullptr;
    }
    if (m_cache) {
        randomx_release_cache(m_cache);
        m_cache = nullptr;
    }
    m_initialized = false;
    m_keyBlockHash = uint256();
}

RandomXContext::~RandomXContext()
{
    LOCK(m_mutex);
    Cleanup();
}

bool RandomXContext::Initialize(const uint256& keyBlockHash)
{
    LOCK(m_mutex);

    // Skip if already initialized with same key
    if (m_initialized && m_keyBlockHash == keyBlockHash) {
        return true;
    }

    // Cleanup any existing state
    Cleanup();

    // Create cache with light mode flags (suitable for validation)
    // randomx_get_flags() auto-detects best optimizations for this CPU
    randomx_flags flags = randomx_get_flags();
    // Light mode uses less memory (256KB vs 2GB) suitable for validation

    // SECURITY FIX [L-03]: Use std::call_once for thread-safe one-time logging
    // Previously used a static bool which had a benign data race (two threads
    // could log simultaneously). Now properly synchronized.
    static std::once_flag logged_capabilities_flag;
    std::call_once(logged_capabilities_flag, [flags]() {
        LogPrintf("RandomX: JIT=%s, HardAES=%s, ARGON2=%s, SSSE3=%s, AVX2=%s\n",
            (flags & RANDOMX_FLAG_JIT) ? "enabled" : "disabled",
            (flags & RANDOMX_FLAG_HARD_AES) ? "enabled" : "disabled",
            (flags & RANDOMX_FLAG_ARGON2) ? "native" : "software",
            (flags & RANDOMX_FLAG_ARGON2_SSSE3) ? "SSSE3" : "off",
            (flags & RANDOMX_FLAG_ARGON2_AVX2) ? "AVX2" : "off");
    });

    m_cache = randomx_alloc_cache(flags);
    if (!m_cache) {
        return false;
    }

    // Initialize cache with key (block hash bytes)
    randomx_init_cache(m_cache, keyBlockHash.begin(), keyBlockHash.size());

    // Create VM in light mode
    m_vm = randomx_create_vm(flags, m_cache, nullptr);
    if (!m_vm) {
        randomx_release_cache(m_cache);
        m_cache = nullptr;
        return false;
    }

    m_keyBlockHash = keyBlockHash;
    m_initialized = true;
    // AUDIT FIX [L-01]: Cache the flags so GetFlags() doesn't call randomx_get_flags() each time.
    m_cached_flags = static_cast<randomx_flags_int>(flags);

    return true;
}

uint256 RandomXContext::CalculateHash(const std::vector<unsigned char>& input)
{
    LOCK(m_mutex);

    if (!m_initialized || !m_vm) {
        throw std::runtime_error("RandomX context not initialized");
    }

    // AUDIT FIX [L-04]: Reduced from 4MB to 1KB. Block headers are ~80 bytes;
    // 4MB was unnecessarily generous and could amplify DoS (each call allocates
    // a 2MB scratchpad). 1KB still provides ample headroom for any legitimate use.
    static constexpr size_t MAX_RANDOMX_INPUT = 1024;
    if (input.size() > MAX_RANDOMX_INPUT) {
        throw std::runtime_error("RandomX input exceeds maximum size");
    }

    // RandomX produces a 256-bit (32-byte) hash
    uint256 result;
    randomx_calculate_hash(m_vm, input.data(), input.size(), result.begin());

    return result;
}

uint256 RandomXContext::CalculateHash(const unsigned char* data, size_t len)
{
    LOCK(m_mutex);

    if (!m_initialized || !m_vm) {
        throw std::runtime_error("RandomX context not initialized");
    }

    // SECURITY FIX [L-05]: Null pointer check on raw input data
    if (!data && len > 0) {
        throw std::runtime_error("RandomX input data is null with non-zero length");
    }

    // Limit input size to prevent DoS attacks
    // AUDIT FIX [L-04]: Reduced from 4MB to 1KB (see vector overload comment).
    static constexpr size_t MAX_RANDOMX_INPUT = 1024;
    if (len > MAX_RANDOMX_INPUT) {
        throw std::runtime_error("RandomX input exceeds maximum size");
    }

    // RandomX produces a 256-bit (32-byte) hash
    uint256 result;
    randomx_calculate_hash(m_vm, data, len, result.begin());

    return result;
}

bool RandomXContext::IsInitialized() const
{
    LOCK(m_mutex);
    return m_initialized;
}

uint256 RandomXContext::GetKeyBlockHash() const
{
    LOCK(m_mutex);
    return m_keyBlockHash;
}

// AUDIT FIX [H-02]: GetCache() removed — see randomx_context.h for rationale.

randomx_flags_int RandomXContext::GetFlags() const
{
    LOCK(m_mutex);
    // AUDIT FIX [L-01]: Return cached flags instead of querying CPU each time.
    return m_cached_flags;
}

// ============================================================================
// RandomXMiningContext - Full dataset mode for efficient mining
// ============================================================================

void RandomXMiningContext::Cleanup()
{
    AssertLockHeld(m_mutex);

    if (m_dataset) {
        // ======================================================================
        // SECURITY FIX [H-06]: Reference-counted dataset deallocation
        // ======================================================================
        //
        // The dataset is wrapped in a shared_ptr. Mining threads that called
        // CreateVM() hold their own copy of this shared_ptr, preventing the
        // dataset from being freed while any VM still references it.
        //
        // Incrementing the epoch BEFORE resetting our shared_ptr ensures mining
        // threads see the epoch change and stop creating new VMs from the old
        // dataset. Existing VMs remain safe because their shared_ptr copy keeps
        // the dataset memory alive until they call randomx_destroy_vm().
        //
        //   Thread A (Cleanup):               Thread B (Mining):
        //   -----------------                 ------------------
        //   epoch.fetch_add(release)  ─────► epoch.load(acquire)
        //   m_dataset.reset()                 if (epoch_changed) destroy VM;
        //                                     dataset stays alive via shared_ptr
        //                                     ... eventually VM destroyed ...
        //                                     last shared_ptr ref dropped → free
        // ======================================================================
        m_dataset_epoch.fetch_add(1, std::memory_order_release);
        LogPrintf("RandomX Mining: Dataset epoch incremented to %lu, releasing dataset reference\n",
                  m_dataset_epoch.load(std::memory_order_relaxed));
        m_dataset.reset(); // Release our reference; mining threads hold their own
    }
    if (m_cache) {
        randomx_release_cache(m_cache);
        m_cache = nullptr;
    }
    m_initialized = false;
    m_keyBlockHash = uint256();
}

RandomXMiningContext::~RandomXMiningContext()
{
    LOCK(m_mutex);
    Cleanup();
}

bool RandomXMiningContext::Initialize(const uint256& keyBlockHash, unsigned int numThreads)
{
    LOCK(m_mutex);

    // SECURITY DOCUMENTATION [L-03]: The mutex is held for the entire initialization,
    // including the ~2GB dataset allocation and multi-thread fill (~30-60 seconds).
    // This is intentional:
    //   1. During initialization, m_cache, m_dataset, m_keyBlockHash, and m_initialized
    //      are all being modified. Releasing the mutex mid-way would expose partially
    //      initialized state to CreateVM() callers.
    //   2. Mining threads that call CreateVM() while Initialize() runs will block,
    //      which is correct — they must not use a half-filled dataset.
    //   3. Initialize() is called rarely (only on key block rotation, every ~32 blocks
    //      per nRandomXKeyBlockInterval) so the long hold time has negligible impact
    //      on throughput.
    //   4. The dataset fill threads spawned below do NOT acquire m_mutex; they only
    //      write to disjoint regions of the already-allocated dataset memory.

    // Skip if already initialized with same key
    if (m_initialized && m_keyBlockHash == keyBlockHash) {
        return true;
    }

    // Cleanup any existing state - MUST happen before new allocation to free ~2GB
    LogPrintf("RandomX Mining: Cleaning up existing state before re-init...\n");
    Cleanup();

    LogPrintf("RandomX Mining: Initializing with %u threads for key %s...\n", 
              numThreads, keyBlockHash.ToString());
    auto startTime = std::chrono::steady_clock::now();

    // Get optimal flags for this CPU
    m_flags = static_cast<randomx_flags_int>(randomx_get_flags());
    // Enable full memory mode for mining (uses ~2GB but much faster)
    m_flags = m_flags | RANDOMX_FLAG_FULL_MEM;
    LogPrintf("RandomX Mining: Using flags=0x%x\n", m_flags);

    // Allocate cache (~256MB)
    LogPrintf("RandomX Mining: Allocating cache...\n");
    m_cache = randomx_alloc_cache(static_cast<randomx_flags>(m_flags));
    if (!m_cache) {
        LogPrintf("RandomX Mining: FATAL - Failed to allocate cache\n");
        return false;
    }
    LogPrintf("RandomX Mining: Cache allocated, initializing with key...\n");

    // Initialize cache with key
    randomx_init_cache(m_cache, keyBlockHash.begin(), keyBlockHash.size());
    LogPrintf("RandomX Mining: Cache initialized\n");

    // Allocate dataset (~2GB)
    LogPrintf("RandomX Mining: Allocating dataset (~2GB)...\n");
    randomx_dataset* raw_dataset = randomx_alloc_dataset(static_cast<randomx_flags>(m_flags));
    if (!raw_dataset) {
        LogPrintf("RandomX Mining: FATAL - Failed to allocate dataset (need ~2GB RAM)\n");
        randomx_release_cache(m_cache);
        m_cache = nullptr;
        return false;
    }
    // SECURITY FIX [H-06]: Wrap dataset in shared_ptr with custom deleter.
    // Mining threads receive a copy of this shared_ptr via CreateVM(), so the
    // dataset is only freed when ALL references (including mining threads) are gone.
    m_dataset = std::shared_ptr<randomx_dataset>(raw_dataset, randomx_release_dataset);

    // Initialize dataset using multiple threads
    // Limit dataset init threads to reduce peak memory from thread stacks
    unsigned int initThreads_count = std::min(numThreads, 4u);
    unsigned long datasetItemCount = randomx_dataset_item_count();
    LogPrintf("RandomX Mining: Dataset allocated, filling with %u init threads (%lu items)...\n", 
              initThreads_count, datasetItemCount);
    if (initThreads_count > 1) {
        std::vector<std::thread> initThreads;
        unsigned long itemsPerThread = datasetItemCount / initThreads_count;
        
        // AUDIT FIX [M-07]: Scope guard ensures all threads are joined even if
        // emplace_back throws (e.g., out of memory for thread stack).
        // Without this, std::thread destructor on a joinable thread calls std::terminate.
        auto thread_guard = [&initThreads]() {
            for (auto& t : initThreads) {
                if (t.joinable()) t.join();
            }
        };
        try {
            for (unsigned int i = 0; i < initThreads_count; ++i) {
                unsigned long startItem = i * itemsPerThread;
                unsigned long itemCount = (i == initThreads_count - 1) 
                    ? (datasetItemCount - startItem) 
                    : itemsPerThread;
                
                LogPrintf("RandomX Mining: Starting init thread %u for items [%lu, %lu)\n", 
                          i, startItem, startItem + itemCount);
                initThreads.emplace_back([this, startItem, itemCount, i]() {
                    LogPrintf("RandomX Mining: Thread %u initializing dataset...\n", i);
                    randomx_init_dataset(m_dataset.get(), m_cache, startItem, itemCount);
                    LogPrintf("RandomX Mining: Thread %u completed\n", i);
                });
            }
        } catch (...) {
            thread_guard();
            throw;
        }
        
        LogPrintf("RandomX Mining: Waiting for %zu init threads to complete...\n", initThreads.size());
        thread_guard();
        LogPrintf("RandomX Mining: All init threads completed\n");
    } else {
        LogPrintf("RandomX Mining: Using single-threaded dataset init\n");
        randomx_init_dataset(m_dataset.get(), m_cache, 0, datasetItemCount);
    }

    m_keyBlockHash = keyBlockHash;
    m_initialized = true;

    auto endTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    LogPrintf("RandomX Mining: Initialized in %lld ms\n", elapsed);

    return true;
}

std::pair<randomx_vm*, std::shared_ptr<void>> RandomXMiningContext::CreateVM()
{
    LOCK(m_mutex);
    
    if (!m_initialized || !m_dataset) {
        return {nullptr, nullptr};
    }

    // Create VM with full dataset (fast mode)
    // Each thread gets its own VM but shares the dataset (read-only)
    auto* vm = randomx_create_vm(static_cast<randomx_flags>(m_flags), nullptr, m_dataset.get());
    // SECURITY FIX [H-06]: Return a copy of the dataset shared_ptr alongside the VM.
    // The caller MUST hold this reference for the lifetime of the VM. This prevents
    // the dataset from being freed during key rotation while a mining thread is still
    // hashing. The shared_ptr ensures the dataset lives until all VMs are destroyed.
    return {vm, m_dataset};
}

bool RandomXMiningContext::IsInitialized() const
{
    LOCK(m_mutex);
    return m_initialized;
}

uint256 RandomXMiningContext::GetKeyBlockHash() const
{
    LOCK(m_mutex);
    return m_keyBlockHash;
}

void InitRandomXContext()
{
    // SECURITY FIX [L-06]: Thread-safe initialization using std::call_once.
    // Previously used a bare if-check which is a data race if called concurrently.
    static std::once_flag g_randomx_init_flag;
    std::call_once(g_randomx_init_flag, []() {
        g_randomx_context = std::make_unique<RandomXContext>();
    });
}

void ShutdownRandomXContext()
{
    // Shutdown is called once on the main thread during node teardown.
    // No race: all validation threads have been joined before this point.
    g_randomx_context.reset();
}
