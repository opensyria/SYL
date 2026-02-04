// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/randomx_pool.h>
#include <logging.h>
#include <util/check.h>

#include <algorithm>

RandomXContextPool g_randomx_pool;

RandomXContextPool::RandomXContextPool() = default;

RandomXContextPool::~RandomXContextPool()
{
    LOCK(m_mutex);
    m_pool.clear();
}

bool RandomXContextPool::ShouldYieldToHigherPriority(AcquisitionPriority my_priority) const
{
    AssertLockHeld(m_mutex);
    
    switch (my_priority) {
        case AcquisitionPriority::LOW:
            // LOW priority yields to NORMAL, HIGH, and CONSENSUS_CRITICAL
            return (m_waiting_normal > 0 || m_waiting_high > 0 || m_waiting_consensus_critical > 0);
        case AcquisitionPriority::NORMAL:
            // Normal priority yields to both HIGH and CONSENSUS_CRITICAL
            return (m_waiting_high > 0 || m_waiting_consensus_critical > 0);
        case AcquisitionPriority::HIGH:
            // High priority only yields to CONSENSUS_CRITICAL
            return (m_waiting_consensus_critical > 0);
        case AcquisitionPriority::CONSENSUS_CRITICAL:
            // Never yields
            return false;
    }
    return false;
}

// FIX 3.1: Time-based priority promotion to prevent starvation
bool RandomXContextPool::ShouldYieldWithStarvationCheck(
    AcquisitionPriority my_priority,
    std::chrono::steady_clock::time_point wait_start) const
{
    AssertLockHeld(m_mutex);
    
    // Check if this request has been waiting too long (starvation prevention)
    auto wait_duration = std::chrono::steady_clock::now() - wait_start;
    if (wait_duration >= STARVATION_PREVENTION_THRESHOLD) {
        // After waiting too long, stop yielding to prevent starvation
        // This effectively promotes the request's priority
        // AUDIT FIX [L-02]: Use member variable instead of static to avoid
        // counter persisting across pool instances and improve testability
        uint64_t count = ++m_starvation_prevention_count;
        if (count <= 10 || count % 100 == 0) {
            LogPrintf("RandomXContextPool: Starvation prevention activated (occurrence #%lu, waited %ld sec)\n",
                      count, std::chrono::duration_cast<std::chrono::seconds>(wait_duration).count());
        }
        return false;
    }
    
    // Otherwise, use normal yielding logic
    return ShouldYieldToHigherPriority(my_priority);
}

std::chrono::seconds RandomXContextPool::GetTimeoutForPriority(AcquisitionPriority priority) const
{
    switch (priority) {
        case AcquisitionPriority::LOW:
            // LOW priority has shortest timeout - background tasks should yield quickly
            return std::chrono::seconds{15};  // FIX L-01
        case AcquisitionPriority::NORMAL:
            return ACQUIRE_TIMEOUT;
        case AcquisitionPriority::HIGH:
            return HIGH_PRIORITY_TIMEOUT;
        case AcquisitionPriority::CONSENSUS_CRITICAL:
            // Return a very long timeout - effectively infinite for practical purposes
            // Using max would cause overflow issues, so use 24 hours
            return std::chrono::seconds{86400};
    }
    return ACQUIRE_TIMEOUT;
}

void RandomXContextPool::IncrementWaitingCount(AcquisitionPriority priority)
{
    AssertLockHeld(m_mutex);
    switch (priority) {
        case AcquisitionPriority::CONSENSUS_CRITICAL:
            m_waiting_consensus_critical++;
            break;
        case AcquisitionPriority::HIGH:
            m_waiting_high++;
            break;
        case AcquisitionPriority::NORMAL:
            m_waiting_normal++;
            break;
        case AcquisitionPriority::LOW:
            m_waiting_low++;
            break;
    }
}

void RandomXContextPool::DecrementWaitingCount(AcquisitionPriority priority)
{
    AssertLockHeld(m_mutex);
    switch (priority) {
        case AcquisitionPriority::CONSENSUS_CRITICAL:
            m_waiting_consensus_critical--;
            break;
        case AcquisitionPriority::HIGH:
            m_waiting_high--;
            break;
        case AcquisitionPriority::NORMAL:
            m_waiting_normal--;
            break;
        case AcquisitionPriority::LOW:
            m_waiting_low--;
            break;
    }
}

std::optional<RandomXContextPool::ContextGuard> RandomXContextPool::Acquire(
    const uint256& keyBlockHash, AcquisitionPriority priority)
{
    WAIT_LOCK(m_mutex, lock);

    const bool is_consensus_critical = (priority == AcquisitionPriority::CONSENSUS_CRITICAL);
    const bool is_high = (priority == AcquisitionPriority::HIGH);
    
    auto timeout = GetTimeoutForPriority(priority);
    auto deadline = std::chrono::steady_clock::now() + timeout;

    // Track waiting threads by priority
    IncrementWaitingCount(priority);

    // RAII cleanup for waiting count - uses helper function for proper thread annotation
    bool wait_decremented{false};
    auto decrement_wait = [this, priority, &wait_decremented]() EXCLUSIVE_LOCKS_REQUIRED(m_mutex) {
        if (wait_decremented) return;
        wait_decremented = true;
        DecrementWaitingCount(priority);
    };
    
    // Simple RAII guard using lambda
    struct WaitGuard {
        decltype(decrement_wait)& decrement_fn;
        WaitGuard(decltype(decrement_wait)& fn) : decrement_fn(fn) {}
        ~WaitGuard() { decrement_fn(); }
    } wait_guard(decrement_wait);

    // Track when we started waiting for starvation prevention
    auto wait_start = std::chrono::steady_clock::now();

    while (true) {
        // Check if we should yield to higher priority waiters
        // (only if there's no context immediately available)
        // Uses starvation-aware check to prevent indefinite blocking
        size_t index = FindOrCreateContext(keyBlockHash);
        
        if (index != SIZE_MAX && !ShouldYieldWithStarvationCheck(priority, wait_start)) {
            // Found or created a context
            m_pool[index].in_use = true;
            m_pool[index].last_used = std::chrono::steady_clock::now();
            m_total_acquisitions++;
            
            if (is_consensus_critical) {
                m_consensus_critical_acquisitions++;
            } else if (is_high) {
                m_high_priority_acquisitions++;
            }

            // Initialize or reinitialize if key changed
            if (m_pool[index].key_hash != keyBlockHash) {
                if (!m_pool[index].context->Initialize(keyBlockHash)) {
                    // Initialization failed - mark as not in use and return error
                    m_pool[index].in_use = false;
                    m_cv.notify_all();  // Wake everyone to retry
                    return std::nullopt;
                }
                m_pool[index].key_hash = keyBlockHash;
                m_key_reinitializations++;
            }

            // Decrement wait counter before returning
            decrement_wait();
            
            return ContextGuard(m_pool[index].context.get(), *this, index);
        }

        // Need to wait - track if we're being preempted
        if (index != SIZE_MAX && ShouldYieldWithStarvationCheck(priority, wait_start)) {
            m_priority_preemptions++;
            LogPrintf("RandomXContextPool: %s priority request yielding to higher priority\n",
                priority == AcquisitionPriority::NORMAL ? "NORMAL" : "HIGH");
        }

        m_total_waits++;

        // For CONSENSUS_CRITICAL, we never timeout - keep waiting
        if (is_consensus_critical) {
            // Wait indefinitely but check periodically for context availability
            m_cv.wait_for(lock, std::chrono::seconds{5});
            // Always retry - consensus critical never gives up
            continue;
        }

        // For other priorities, respect timeout
        if (m_cv.wait_until(lock, deadline) == std::cv_status::timeout) {
            m_total_timeouts++;
            LogPrintf("RandomXContextPool: Timeout waiting for context (priority=%s, active=%zu, waiting_cc=%zu)\n",
                is_high ? "HIGH" : "NORMAL",
                std::count_if(m_pool.begin(), m_pool.end(), [](const PoolEntry& e) { return e.in_use; }),
                m_waiting_consensus_critical);
            return std::nullopt;
        }
    }
}

size_t RandomXContextPool::FindOrCreateContext(const uint256& keyBlockHash)
{
    AssertLockHeld(m_mutex);

    // First, look for an available context with matching key (best case)
    for (size_t i = 0; i < m_pool.size(); ++i) {
        if (!m_pool[i].in_use && m_pool[i].key_hash == keyBlockHash) {
            return i;
        }
    }

    // Second, look for any available context
    for (size_t i = 0; i < m_pool.size(); ++i) {
        if (!m_pool[i].in_use) {
            return i;
        }
    }

    // Third, if pool isn't full, create a new context
    if (m_pool.size() < m_max_contexts) {
        PoolEntry entry;
        entry.context = std::make_unique<RandomXContext>();
        entry.in_use = false;
        m_pool.push_back(std::move(entry));
        
        // Update peak memory tracking (audit recommendation)
        size_t current_memory = m_pool.size() * CONTEXT_MEMORY_ESTIMATE;
        if (current_memory > m_peak_memory_bytes) {
            m_peak_memory_bytes = current_memory;
        }
        
        return m_pool.size() - 1;
    }

    // Pool is full and all contexts are in use
    return SIZE_MAX;
}

void RandomXContextPool::Return(size_t index)
{
    if (index == SIZE_MAX) return;

    {
        LOCK(m_mutex);
        if (index < m_pool.size()) {
            m_pool[index].in_use = false;
            m_pool[index].last_used = std::chrono::steady_clock::now();
        }
    }
    // Notify all waiters - priority is handled in Acquire()
    m_cv.notify_all();
}

RandomXContextPool::PoolStats RandomXContextPool::GetStats() const
{
    LOCK(m_mutex);

    PoolStats stats;
    stats.total_contexts = m_pool.size();
    stats.active_contexts = std::count_if(m_pool.begin(), m_pool.end(),
        [](const PoolEntry& e) { return e.in_use; });
    stats.available_contexts = stats.total_contexts - stats.active_contexts;
    stats.total_acquisitions = m_total_acquisitions;
    stats.total_waits = m_total_waits;
    stats.total_timeouts = m_total_timeouts;
    stats.key_reinitializations = m_key_reinitializations;
    stats.consensus_critical_acquisitions = m_consensus_critical_acquisitions;
    stats.high_priority_acquisitions = m_high_priority_acquisitions;
    stats.priority_preemptions = m_priority_preemptions;
    
    // Memory monitoring (audit recommendation)
    stats.estimated_memory_bytes = stats.total_contexts * CONTEXT_MEMORY_ESTIMATE;
    stats.peak_memory_bytes = m_peak_memory_bytes;
    stats.max_allowed_contexts = m_max_contexts;

    return stats;
}

bool RandomXContextPool::SetMaxContexts(size_t max_contexts)
{
    LOCK(m_mutex);

    // Can only change before any contexts are created
    if (!m_pool.empty()) {
        return false;
    }

    if (max_contexts == 0 || max_contexts > 64) {
        return false;  // Sanity bounds
    }

    m_max_contexts = max_contexts;
    return true;
}
