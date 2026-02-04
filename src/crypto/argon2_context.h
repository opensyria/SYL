// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENSY_CRYPTO_ARGON2_CONTEXT_H
#define OPENSY_CRYPTO_ARGON2_CONTEXT_H

#include <uint256.h>
#include <sync.h>
#include <primitives/block.h>

#include <cstdint>
#include <memory>
#include <vector>

// Forward declaration
namespace Consensus { struct Params; }

/**
 * Argon2id Proof-of-Work Context
 *
 * This is the EMERGENCY FALLBACK algorithm for OpenSY.
 * Only activated if RandomX is compromised (cryptographic break, critical vuln).
 *
 * Argon2id is the winner of the Password Hashing Competition (2015) and provides:
 *   - Memory-hardness (ASIC/GPU resistant when configured with high memory)
 *   - Side-channel resistance (the "id" hybrid variant)
 *   - Simplicity (smaller attack surface than RandomX)
 *   - Wide audit coverage (1Password, Bitwarden, Signal, Cloudflare use it)
 *
 * PARAMETERS (consensus-critical):
 *   - Memory: 2GB (matches RandomX for fair CPU mining)
 *   - Time cost: 1 iteration (tuned for ~100ms per hash)
 *   - Parallelism: 1 (prevents GPU optimization)
 *   - Hash length: 32 bytes (256-bit)
 *
 * ACTIVATION:
 *   - Via consensus.nArgon2EmergencyHeight (default: -1 = never)
 *   - Can be activated via hard fork if RandomX is compromised
 *
 * IMPLEMENTATION NOTES:
 *   - Uses libsodium's crypto_pwhash_argon2id (if available)
 *   - Fallback to reference Argon2 implementation
 *   - Thread-safe via mutex protection
 */
class Argon2Context
{
private:
    mutable Mutex m_mutex;
    bool m_initialized{false};

    // Argon2id parameters (consensus-critical)
    uint32_t m_memory_cost;    //!< Memory in KiB
    uint32_t m_time_cost;      //!< Number of iterations
    uint32_t m_parallelism;    //!< Parallelism factor

    static constexpr size_t HASH_LENGTH = 32; //!< 256-bit output

public:
    /**
     * Construct Argon2 context with consensus parameters.
     * @param memory_cost Memory in KiB (e.g., 2097152 for 2GB)
     * @param time_cost   Number of iterations (1 recommended for PoW)
     * @param parallelism Parallelism factor (1 to prevent GPU advantage)
     */
    Argon2Context(uint32_t memory_cost = (1 << 21),
                  uint32_t time_cost = 1,
                  uint32_t parallelism = 1);

    ~Argon2Context() = default;

    // Non-copyable, non-movable (heavy resource)
    Argon2Context(const Argon2Context&) = delete;
    Argon2Context& operator=(const Argon2Context&) = delete;
    Argon2Context(Argon2Context&&) = delete;
    Argon2Context& operator=(Argon2Context&&) = delete;

    /**
     * Calculate Argon2id hash for proof-of-work.
     *
     * @param input   Block header data to hash
     * @param salt    Salt for Argon2 (use previous block hash for uniqueness)
     * @return        256-bit hash suitable for PoW comparison
     *
     * SECURITY: The salt MUST be unique per block to prevent precomputation.
     *           Using prevBlockHash as salt provides this property.
     */
    uint256 CalculateHash(const std::vector<unsigned char>& input,
                          const uint256& salt) const;

    /**
     * Convenience overload for raw data pointers.
     */
    uint256 CalculateHash(const unsigned char* data, size_t len,
                          const uint256& salt) const;

    /**
     * Calculate Argon2id PoW hash for a block header.
     * Uses hashPrevBlock as the salt for Argon2.
     *
     * @param header  Block header to hash
     * @return        256-bit PoW hash
     */
    uint256 CalculateBlockHash(const CBlockHeader& header) const;

    /** Check if context is ready for hashing */
    bool IsInitialized() const;

    /** Get current memory cost in KiB */
    uint32_t GetMemoryCost() const { return m_memory_cost; }

    /** Get current time cost (iterations) */
    uint32_t GetTimeCost() const { return m_time_cost; }

    /** Get current parallelism factor */
    uint32_t GetParallelism() const { return m_parallelism; }
};

/**
 * Global Argon2 context for emergency PoW fallback.
 * Lazily initialized only if Argon2 emergency mode is activated.
 */
extern std::unique_ptr<Argon2Context> g_argon2_context;

/**
 * Initialize the global Argon2 context with consensus parameters.
 * Called during node startup if Argon2 emergency mode is pending/active.
 */
void InitArgon2Context(uint32_t memory_cost, uint32_t time_cost, uint32_t parallelism);

/**
 * Calculate Argon2id PoW hash for a block header.
 * Initializes global context if needed.
 *
 * @param header Block header to hash
 * @param params Consensus parameters for Argon2 configuration
 * @return       256-bit PoW hash
 */
uint256 CalculateArgon2Hash(const CBlockHeader& header, const Consensus::Params& params);

#endif // OPENSY_CRYPTO_ARGON2_CONTEXT_H
