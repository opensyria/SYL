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

    return true;
}

uint256 RandomXContext::CalculateHash(const std::vector<unsigned char>& input)
{
    LOCK(m_mutex);

    if (!m_initialized || !m_vm) {
        throw std::runtime_error("RandomX context not initialized");
    }

    // Limit input size to prevent DoS attacks
    // Block headers are 80 bytes; allow generous margin for other uses
    static constexpr size_t MAX_RANDOMX_INPUT = 4 * 1024 * 1024; // 4MB
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

    // Limit input size to prevent DoS attacks
    static constexpr size_t MAX_RANDOMX_INPUT = 4 * 1024 * 1024; // 4MB
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

randomx_cache* RandomXContext::GetCache() const
{
    LOCK(m_mutex);
    return m_cache;
}

randomx_flags_int RandomXContext::GetFlags() const
{
    LOCK(m_mutex);
    return static_cast<randomx_flags_int>(randomx_get_flags());
}

// ============================================================================
// RandomXMiningContext - Full dataset mode for efficient mining
// ============================================================================

void RandomXMiningContext::Cleanup()
{
    AssertLockHeld(m_mutex);

    if (m_dataset) {
        // ======================================================================
        // MEMORY BARRIER DOCUMENTATION (M-02 Audit Finding)
        // ======================================================================
        // 
        // CRITICAL: The epoch increment MUST happen BEFORE freeing the dataset.
        // This creates a happens-before relationship that prevents use-after-free:
        //
        //   Thread A (Cleanup):               Thread B (Mining):
        //   -----------------                 ------------------
        //   epoch.fetch_add(release)  ─────► epoch.load(acquire)
        //   [memory barrier]                  [memory barrier]
        //   release_dataset()                 if (epoch_changed) abort;
        //                                     else: hash = VM.calculate() ← SAFE
        //
        // The release-acquire pair ensures Thread B sees the incremented epoch
        // before Thread A's subsequent free becomes visible to Thread B.
        //
        // RACE WINDOW ANALYSIS:
        // A theoretical race exists if Thread B passes the epoch check but is
        // preempted for longer than it takes to: increment epoch + free dataset
        // + allocate new dataset. This is extremely unlikely (~seconds) and only
        // affects mining (temporary hash failure), not consensus validation.
        // ======================================================================
        m_dataset_epoch.fetch_add(1, std::memory_order_release);
        LogPrintf("RandomX Mining: Dataset epoch incremented to %lu, freeing old dataset\n", 
                  m_dataset_epoch.load(std::memory_order_relaxed));
        randomx_release_dataset(m_dataset);
        m_dataset = nullptr;
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
    m_dataset = randomx_alloc_dataset(static_cast<randomx_flags>(m_flags));
    if (!m_dataset) {
        LogPrintf("RandomX Mining: FATAL - Failed to allocate dataset (need ~2GB RAM)\n");
        randomx_release_cache(m_cache);
        m_cache = nullptr;
        return false;
    }

    // Initialize dataset using multiple threads
    // Limit dataset init threads to reduce peak memory from thread stacks
    unsigned int initThreads_count = std::min(numThreads, 4u);
    unsigned long datasetItemCount = randomx_dataset_item_count();
    LogPrintf("RandomX Mining: Dataset allocated, filling with %u init threads (%lu items)...\n", 
              initThreads_count, datasetItemCount);
    if (initThreads_count > 1) {
        std::vector<std::thread> initThreads;
        unsigned long itemsPerThread = datasetItemCount / initThreads_count;
        
        for (unsigned int i = 0; i < initThreads_count; ++i) {
            unsigned long startItem = i * itemsPerThread;
            unsigned long itemCount = (i == initThreads_count - 1) 
                ? (datasetItemCount - startItem) 
                : itemsPerThread;
            
            LogPrintf("RandomX Mining: Starting init thread %u for items [%lu, %lu)\n", 
                      i, startItem, startItem + itemCount);
            initThreads.emplace_back([this, startItem, itemCount, i]() {
                LogPrintf("RandomX Mining: Thread %u initializing dataset...\n", i);
                randomx_init_dataset(m_dataset, m_cache, startItem, itemCount);
                LogPrintf("RandomX Mining: Thread %u completed\n", i);
            });
        }
        
        LogPrintf("RandomX Mining: Waiting for %zu init threads to complete...\n", initThreads.size());
        for (auto& t : initThreads) {
            t.join();
        }
        LogPrintf("RandomX Mining: All init threads completed\n");
    } else {
        LogPrintf("RandomX Mining: Using single-threaded dataset init\n");
        randomx_init_dataset(m_dataset, m_cache, 0, datasetItemCount);
    }

    m_keyBlockHash = keyBlockHash;
    m_initialized = true;

    auto endTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    LogPrintf("RandomX Mining: Initialized in %lld ms\n", elapsed);

    return true;
}

randomx_vm* RandomXMiningContext::CreateVM()
{
    LOCK(m_mutex);
    
    if (!m_initialized || !m_dataset) {
        return nullptr;
    }

    // Create VM with full dataset (fast mode)
    // Each thread gets its own VM but shares the dataset (read-only)
    return randomx_create_vm(static_cast<randomx_flags>(m_flags), nullptr, m_dataset);
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
    if (!g_randomx_context) {
        g_randomx_context = std::make_unique<RandomXContext>();
    }
}

void ShutdownRandomXContext()
{
    if (g_randomx_context) {
        g_randomx_context.reset();
    }
}
