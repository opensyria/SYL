// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENSY_CRYPTO_RANDOMX_POOL_H
#define OPENSY_CRYPTO_RANDOMX_POOL_H

#include <crypto/randomx_context.h>
#include <sync.h>
#include <uint256.h>
#include <util/time.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <optional>
#include <vector>

/**
 * Priority levels for context acquisition.
 *
 * CONSENSUS_CRITICAL: Used for block validation - these must never timeout
 *                     to prevent rejecting valid blocks under load.
 * HIGH: Used for mining and important operations.
 * NORMAL: Used for RPC queries and non-critical operations.
 * LOW: Used for background tasks like prefetching or prewarming (FIX L-01)
 */
enum class AcquisitionPriority {
    LOW = 0,             // FIX L-01: Background tasks, will yield to all others
    NORMAL = 1,
    HIGH = 2,
    CONSENSUS_CRITICAL = 3
};

/**
 * A bounded pool of RandomX contexts to prevent unbounded memory growth.
 *
 * SECURITY FIX [H-01]: Thread-Local RandomX Context Memory Accumulation
 *
 * Previously, each thread had its own thread_local RandomX context (~256KB each),
 * leading to unbounded memory growth under high concurrency. This pool:
 *
 * 1. Limits the total number of contexts to MAX_CONTEXTS
 * 2. Uses RAII guards for automatic checkout/checkin
 * 3. Implements key-aware context reuse (LRU eviction)
 * 4. Blocks threads when pool is exhausted (bounded memory)
 * 5. Supports priority-based acquisition for consensus-critical operations
 *
 * Usage:
 *   auto guard = g_randomx_pool.Acquire(keyBlockHash);
 *   uint256 hash = guard->CalculateHash(data, len);
 *   // Context automatically returned to pool when guard destructs
 *
 * Priority Usage:
 *   // For block validation (consensus-critical, never times out)
 *   auto guard = g_randomx_pool.Acquire(keyBlockHash, AcquisitionPriority::CONSENSUS_CRITICAL);
 */
class RandomXContextPool
{
public:
    //! Maximum number of contexts in the pool
    //! Tune based on expected parallelism and available memory
    //! 8 contexts * 256KB = 2MB maximum memory usage
    static constexpr size_t MAX_CONTEXTS = 8;

    //! Timeout for acquiring a context (prevents deadlock)
    //! Only applies to NORMAL and HIGH priority requests
    static constexpr std::chrono::seconds ACQUIRE_TIMEOUT{30};

    //! Short timeout for LOW priority requests (background tasks)
    //! FIX L-01: LOW priority should yield quickly to higher priorities
    static constexpr std::chrono::seconds LOW_PRIORITY_TIMEOUT{15};

    //! Extended timeout for HIGH priority requests
    static constexpr std::chrono::seconds HIGH_PRIORITY_TIMEOUT{120};
    
    //! FIX 3.1: Maximum time a NORMAL priority request will yield before being promoted
    //! This prevents starvation under sustained high-priority load
    static constexpr std::chrono::seconds STARVATION_PREVENTION_THRESHOLD{10};

    /**
     * RAII guard that holds a context and returns it to the pool on destruction.
     */
    class ContextGuard
    {
    public:
        ContextGuard(RandomXContext* ctx, RandomXContextPool& pool, size_t index)
            : m_ctx(ctx), m_pool(pool), m_index(index) {}

        ~ContextGuard() {
            // AUDIT FIX [I-01]: Guard against returning moved-from contexts
            // After move construction, m_index is set to SIZE_MAX to indicate
            // this guard no longer owns a context
            if (m_index != SIZE_MAX) {
                m_pool.Return(m_index);
            }
        }

        // Non-copyable
        ContextGuard(const ContextGuard&) = delete;
        ContextGuard& operator=(const ContextGuard&) = delete;

        // Movable
        ContextGuard(ContextGuard&& other) noexcept
            : m_ctx(other.m_ctx), m_pool(other.m_pool), m_index(other.m_index)
        {
            other.m_ctx = nullptr;
            other.m_index = SIZE_MAX;
        }

        // SECURITY FIX [L-01]: Move assignment is explicitly deleted because
        // ContextGuard holds a reference to a specific pool slot (m_index).
        // Assignment would require returning the old slot and claiming the new one,
        // which is error-prone with pool state. Move construction is sufficient
        // for std::optional<ContextGuard> usage in Acquire().
        ContextGuard& operator=(ContextGuard&&) = delete;

        //! Access the underlying context
        RandomXContext* operator->() const { return m_ctx; }
        RandomXContext& operator*() const { return *m_ctx; }
        RandomXContext* get() const { return m_ctx; }

    private:
        RandomXContext* m_ctx;
        RandomXContextPool& m_pool;
        size_t m_index;
    };

    RandomXContextPool();
    ~RandomXContextPool();

    // Non-copyable, non-movable
    RandomXContextPool(const RandomXContextPool&) = delete;
    RandomXContextPool& operator=(const RandomXContextPool&) = delete;

    /**
     * Acquire a context from the pool, initialized with the given key.
     *
     * If the pool is exhausted, this will block until a context becomes available
     * or the timeout expires (for NORMAL/HIGH priority).
     *
     * CONSENSUS_CRITICAL priority requests will:
     * - Never timeout (prevents valid block rejection)
     * - Be served before NORMAL priority requests
     * - Preempt waiting NORMAL priority requests
     *
     * @param[in] keyBlockHash The RandomX key block hash
     * @param[in] priority     The acquisition priority (default: NORMAL)
     * @return A guard holding the context, or nullopt on timeout (never for CONSENSUS_CRITICAL)
     */
    std::optional<ContextGuard> Acquire(const uint256& keyBlockHash, 
                                         AcquisitionPriority priority = AcquisitionPriority::NORMAL) 
        EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /**
     * Get current pool statistics for monitoring.
     */
    struct PoolStats {
        size_t total_contexts;      //!< Total contexts created
        size_t active_contexts;     //!< Currently checked out
        size_t available_contexts;  //!< Ready for use
        size_t total_acquisitions;  //!< Total successful acquires
        size_t total_waits;         //!< Times a thread had to wait
        size_t total_timeouts;      //!< Times acquisition timed out
        size_t key_reinitializations; //!< Times a context was reinitialized for new key
        size_t consensus_critical_acquisitions; //!< Consensus-critical acquisitions
        size_t high_priority_acquisitions;      //!< High priority acquisitions
        size_t priority_preemptions;            //!< Times high-priority preempted normal
        // Memory monitoring (audit recommendation)
        size_t estimated_memory_bytes; //!< Estimated current memory usage (~256KB per context)
        size_t peak_memory_bytes;      //!< Peak memory usage observed
        size_t max_allowed_contexts;   //!< Configured maximum contexts
    };

    PoolStats GetStats() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /**
     * Configure the maximum number of contexts.
     * Can only be called before any contexts are acquired.
     */
    bool SetMaxContexts(size_t max_contexts) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

private:
    struct PoolEntry {
        std::unique_ptr<RandomXContext> context;
        uint256 key_hash;
        std::chrono::steady_clock::time_point last_used;
        bool in_use{false};
    };

    mutable Mutex m_mutex;
    std::condition_variable_any m_cv;  //!< Uses condition_variable_any to work with Bitcoin Core's Mutex wrapper
    std::vector<PoolEntry> m_pool GUARDED_BY(m_mutex);
    size_t m_max_contexts GUARDED_BY(m_mutex){MAX_CONTEXTS};

    //! Priority queue tracking
    size_t m_waiting_consensus_critical GUARDED_BY(m_mutex){0};
    size_t m_waiting_high GUARDED_BY(m_mutex){0};
    size_t m_waiting_normal GUARDED_BY(m_mutex){0};
    size_t m_waiting_low GUARDED_BY(m_mutex){0};  //!< FIX L-01: Track LOW priority waiters

    // Statistics
    size_t m_total_acquisitions GUARDED_BY(m_mutex){0};
    size_t m_total_waits GUARDED_BY(m_mutex){0};
    size_t m_total_timeouts GUARDED_BY(m_mutex){0};
    size_t m_key_reinitializations GUARDED_BY(m_mutex){0};
    size_t m_consensus_critical_acquisitions GUARDED_BY(m_mutex){0};
    size_t m_high_priority_acquisitions GUARDED_BY(m_mutex){0};
    size_t m_priority_preemptions GUARDED_BY(m_mutex){0};
    
    // AUDIT FIX [L-02]: Starvation prevention counter as mutable member
    // Previously was static, now properly scoped to pool instance for testability
    mutable std::atomic<uint64_t> m_starvation_prevention_count{0};
    
    // Memory monitoring (audit recommendation)
    static constexpr size_t CONTEXT_MEMORY_ESTIMATE = 256 * 1024; // ~256KB per context
    size_t m_peak_memory_bytes GUARDED_BY(m_mutex){0};

    /**
     * Return a context to the pool.
     * Called by ContextGuard destructor.
     */
    void Return(size_t index) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /**
     * Find or create a context for the given key.
     * Returns the index of the context, or SIZE_MAX if none available.
     */
    size_t FindOrCreateContext(const uint256& keyBlockHash) EXCLUSIVE_LOCKS_REQUIRED(m_mutex);

    /**
     * Check if this priority level should yield to higher priority waiters.
     */
    bool ShouldYieldToHigherPriority(AcquisitionPriority my_priority) const EXCLUSIVE_LOCKS_REQUIRED(m_mutex);

    /**
     * FIX 3.1: Check if should yield, with starvation prevention.
     * After STARVATION_PREVENTION_THRESHOLD, stops yielding to prevent starvation.
     */
    bool ShouldYieldWithStarvationCheck(
        AcquisitionPriority my_priority,
        std::chrono::steady_clock::time_point wait_start) const EXCLUSIVE_LOCKS_REQUIRED(m_mutex);

    /**
     * Get timeout duration based on priority.
     */
    std::chrono::seconds GetTimeoutForPriority(AcquisitionPriority priority) const;

    /**
     * Increment waiting count for the given priority.
     * Thread-safe helper with proper annotations.
     */
    void IncrementWaitingCount(AcquisitionPriority priority) EXCLUSIVE_LOCKS_REQUIRED(m_mutex);

    /**
     * Decrement waiting count for the given priority.
     * Thread-safe helper with proper annotations.
     */
    void DecrementWaitingCount(AcquisitionPriority priority) EXCLUSIVE_LOCKS_REQUIRED(m_mutex);
};

//! Global RandomX context pool instance
extern RandomXContextPool g_randomx_pool;

#endif // OPENSY_CRYPTO_RANDOMX_POOL_H
