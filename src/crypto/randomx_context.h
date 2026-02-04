// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENSY_CRYPTO_RANDOMX_CONTEXT_H
#define OPENSY_CRYPTO_RANDOMX_CONTEXT_H

#include <sync.h>
#include <uint256.h>

#include <cstddef>
#include <memory>
#include <vector>

// Forward declarations for RandomX types to avoid including randomx.h in header
struct randomx_cache;
struct randomx_vm;
struct randomx_dataset;
// randomx_flags is an enum, so we use int for the interface
using randomx_flags_int = int;

/**
 * Thread-safe RandomX context manager for proof-of-work hash calculation.
 *
 * RandomX is an ASIC-resistant, CPU-optimized proof-of-work algorithm
 * used to democratize mining by making it accessible to commodity hardware.
 *
 * The algorithm requires initialization with a "key" derived from a recent
 * block hash. This key changes periodically (every 64 blocks by default)
 * to prevent pre-computation attacks.
 *
 * Usage:
 *   RandomXContext ctx;
 *   ctx.Initialize(keyBlockHash);
 *   uint256 hash = ctx.CalculateHash(blockHeaderData);
 *
 * Thread Safety:
 *   All public methods are thread-safe and can be called concurrently.
 */
class RandomXContext
{
private:
    //! RandomX dataset cache (256 KB for light mode)
    randomx_cache* m_cache{nullptr};

    //! RandomX virtual machine instance
    randomx_vm* m_vm{nullptr};

    //! Hash of the block used as RandomX key
    uint256 m_keyBlockHash;

    //! Mutex for thread-safe access
    mutable Mutex m_mutex;

    //! Flag indicating if context is ready for hashing
    bool m_initialized{false};

    //! Cleanup internal resources
    void Cleanup() EXCLUSIVE_LOCKS_REQUIRED(m_mutex);

public:
    RandomXContext() = default;
    ~RandomXContext() EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    // Non-copyable, non-movable
    RandomXContext(const RandomXContext&) = delete;
    RandomXContext& operator=(const RandomXContext&) = delete;
    RandomXContext(RandomXContext&&) = delete;
    RandomXContext& operator=(RandomXContext&&) = delete;

    /**
     * Initialize or reinitialize the RandomX context with a new key.
     *
     * This operation is expensive (~1 second) as it rebuilds the internal
     * cache. Should only be called when the key block changes.
     *
     * @param[in] keyBlockHash Hash of the block to use as RandomX key.
     *                         Typically the block at (height - height % 64 - 64).
     * @return true if initialization succeeded, false on error.
     */
    bool Initialize(const uint256& keyBlockHash) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /**
     * Calculate the RandomX hash of input data.
     *
     * @param[in] input Raw bytes to hash (typically serialized block header).
     * @return 256-bit RandomX hash of the input.
     * @throws std::runtime_error if context is not initialized.
     */
    uint256 CalculateHash(const std::vector<unsigned char>& input) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /**
     * Calculate the RandomX hash of input data (raw pointer version).
     *
     * @param[in] data Pointer to bytes to hash.
     * @param[in] len Length of data in bytes.
     * @return 256-bit RandomX hash of the input.
     * @throws std::runtime_error if context is not initialized.
     */
    uint256 CalculateHash(const unsigned char* data, size_t len) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /**
     * Check if the context is initialized and ready for hashing.
     *
     * @return true if Initialize() has been called successfully.
     */
    bool IsInitialized() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /**
     * Get the current key block hash.
     *
     * @return The hash used to initialize this context, or uint256() if not initialized.
     */
    uint256 GetKeyBlockHash() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /**
     * Get raw cache pointer for creating additional VMs.
     * The cache must remain valid for the lifetime of any VMs created from it.
     */
    randomx_cache* GetCache() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /**
     * Get the flags used for this context.
     */
    randomx_flags_int GetFlags() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
};

/**
 * Mining-optimized RandomX context with per-thread VM support.
 * Uses full dataset mode (2GB) for maximum hash rate.
 *
 * THREAD SAFETY:
 * VMs created by CreateVM() hold pointers to the internal dataset.
 * When Initialize() is called with a new key, the dataset is reallocated,
 * invalidating any existing VMs. The epoch counter tracks dataset generations
 * so mining threads can detect when their VM is stale and must be recreated.
 */
class RandomXMiningContext
{
private:
    randomx_cache* m_cache{nullptr};
    randomx_dataset* m_dataset{nullptr};
    uint256 m_keyBlockHash;
    randomx_flags_int m_flags{0};
    mutable Mutex m_mutex;
    bool m_initialized{false};
    
    //! Dataset epoch counter - incremented each time dataset is reallocated.
    //! Mining threads must check this to detect stale VMs and avoid use-after-free.
    std::atomic<uint64_t> m_dataset_epoch{0};

    void Cleanup() EXCLUSIVE_LOCKS_REQUIRED(m_mutex);

public:
    RandomXMiningContext() = default;
    ~RandomXMiningContext() EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    RandomXMiningContext(const RandomXMiningContext&) = delete;
    RandomXMiningContext& operator=(const RandomXMiningContext&) = delete;

    /**
     * Initialize with full dataset for mining (uses ~2GB RAM).
     * @param keyBlockHash The key block hash for RandomX
     * @param numThreads Number of threads to use for dataset init
     * @return true on success
     */
    bool Initialize(const uint256& keyBlockHash, unsigned int numThreads = 1) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /**
     * Create a new VM instance for a mining thread.
     * Caller owns the returned VM and must destroy it with randomx_destroy_vm().
     * Thread-safe: multiple threads can call this concurrently.
     */
    randomx_vm* CreateVM() EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    bool IsInitialized() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    uint256 GetKeyBlockHash() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    
    /**
     * Get the current dataset epoch.
     * Mining threads should capture this when creating a VM and periodically
     * check if it has changed. If it has, the VM is stale and must be destroyed.
     * This is lock-free for performance in the mining hot path.
     */
    uint64_t GetDatasetEpoch() const { return m_dataset_epoch.load(std::memory_order_acquire); }
};

/**
 * Global RandomX context for block validation (light mode).
 *
 * This singleton context is used throughout the codebase for validating
 * incoming blocks. It uses RandomX "light" mode which requires only 256 KB
 * of memory, suitable for verification.
 *
 * For mining, a separate context with "full" mode (2 GB) should be used.
 */
extern std::unique_ptr<RandomXContext> g_randomx_context;

/**
 * Initialize the global RandomX validation context.
 *
 * Should be called during node startup after chain state is loaded.
 * Safe to call multiple times (subsequent calls are no-ops).
 */
void InitRandomXContext();

/**
 * Shutdown and cleanup the global RandomX context.
 *
 * Should be called during node shutdown to release resources.
 */
void ShutdownRandomXContext();

#endif // OPENSY_CRYPTO_RANDOMX_CONTEXT_H
