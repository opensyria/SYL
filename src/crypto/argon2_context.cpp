// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/argon2_context.h>
#include <consensus/params.h>
#include <logging.h>
#include <streams.h>
#include <util/check.h>

#include <mutex>

// Argon2 reference implementation
// Argon2id implementation via libsodium
// libsodium is REQUIRED for mainnet builds to ensure proper memory-hard PoW
// if the emergency Argon2 fallback is ever activated.
//
// Install libsodium:
//   macOS:  brew install libsodium
//   Ubuntu: apt install libsodium-dev
//   Fedora: dnf install libsodium-devel
//
#ifdef HAVE_LIBSODIUM
#include <sodium.h>
#define USE_LIBSODIUM 1
#else
// SECURITY: SHA256 fallback is ONLY allowed for debug/regtest builds
// Mainnet and testnet release builds MUST have libsodium for Argon2id emergency PoW
#define USE_LIBSODIUM 0
#include <crypto/sha256.h>
#include <common/args.h>
#include <util/chaintype.h>

// Compile-time check: Release builds REQUIRE libsodium - no exceptions
// The weak fallback escape hatch has been removed for security.
// Install libsodium: brew install libsodium (macOS) / apt install libsodium-dev (Ubuntu)
#if defined(NDEBUG)
#error "Building release without libsodium is not allowed. Argon2id emergency PoW requires libsodium. \
Install libsodium (brew install libsodium / apt install libsodium-dev) before building for production."
#endif
#endif

#include <stdexcept>

std::unique_ptr<Argon2Context> g_argon2_context;

Argon2Context::Argon2Context(uint32_t memory_cost, uint32_t time_cost, uint32_t parallelism)
    : m_memory_cost(memory_cost), m_time_cost(time_cost), m_parallelism(parallelism)
{
    // Validate parameters
    if (memory_cost < 8) {
        throw std::invalid_argument("Argon2 memory_cost must be at least 8 KiB");
    }
    if (time_cost < 1) {
        throw std::invalid_argument("Argon2 time_cost must be at least 1");
    }
    if (parallelism < 1) {
        throw std::invalid_argument("Argon2 parallelism must be at least 1");
    }

#if USE_LIBSODIUM
    if (sodium_init() < 0) {
        throw std::runtime_error("Failed to initialize libsodium");
    }
#endif

    m_initialized = true;

    LogPrintf("Argon2Context: Initialized with memory=%u KiB, time=%u, parallelism=%u\n",
              m_memory_cost, m_time_cost, m_parallelism);
}

uint256 Argon2Context::CalculateHash(const std::vector<unsigned char>& input,
                                      const uint256& salt) const
{
    return CalculateHash(input.data(), input.size(), salt);
}

uint256 Argon2Context::CalculateHash(const unsigned char* data, size_t len,
                                      const uint256& salt) const
{
    LOCK(m_mutex);

    if (!m_initialized) {
        throw std::runtime_error("Argon2 context not initialized");
    }

    // Limit input size to prevent DoS
    static constexpr size_t ARGON2_MAX_INPUT_SIZE = 4 * 1024 * 1024; // 4MB
    if (len > ARGON2_MAX_INPUT_SIZE) {
        throw std::runtime_error("Argon2 input exceeds maximum size");
    }

    uint256 result;

#if USE_LIBSODIUM
    // Use libsodium's Argon2id implementation
    // crypto_pwhash with ALG_ARGON2ID13
    int ret = crypto_pwhash(
        result.begin(),                           // output
        HASH_LENGTH,                              // output length
        reinterpret_cast<const char*>(data),      // password (block header)
        len,                                      // password length
        salt.begin(),                             // salt (prev block hash)
        m_time_cost,                              // opslimit (iterations)
        static_cast<size_t>(m_memory_cost) * 1024,// memlimit (bytes)
        crypto_pwhash_ALG_ARGON2ID13              // algorithm
    );

    if (ret != 0) {
        throw std::runtime_error("Argon2id hash calculation failed");
    }
#else
    // DEVELOPMENT/TESTING FALLBACK - SHA256 (NOT memory-hard!)
    // This fallback exists ONLY to allow compilation without libsodium for testing.
    //
    // SECURITY CRITICAL:
    // - Real Argon2id requires 2GB memory, making GPU/ASIC attacks expensive
    // - SHA256 is trivially GPU-parallelizable
    // - If this fallback runs on mainnet during an Argon2 emergency fork,
    //   attackers with GPUs could mine orders of magnitude faster than CPUs
    //
    // The Argon2 emergency mode is DORMANT (nArgon2EmergencyHeight = -1).
    // If ever activated, ALL nodes MUST have libsodium or network will fork.
    //
    // SECURITY FIX [M-15]: In debug builds, abort if this fallback is used on
    // mainnet or testnet. This prevents developers from accidentally validating
    // Argon2 activation scenarios with the wrong hash function and thinking
    // everything works when it would actually cause a consensus fork.
    // Only regtest is allowed to use the SHA256 fallback silently.
    assert(gArgs.GetChainType() == ChainType::REGTEST &&
           "Argon2 SHA256 fallback must not be used on mainnet/testnet! Install libsodium.");

    // FIX 2.3: Log warning on EVERY call (not just first) since this is critical
    // A single warning at startup could be missed in log rotation
    static std::atomic<uint64_t> weak_hash_count{0};
    uint64_t count = ++weak_hash_count;
    
    // Log every call for first 10, then every 1000th call
    if (count <= 10 || count % 1000 == 0) {
        LogPrintf("*** CRITICAL: Argon2id using WEAK SHA256 fallback (call #%lu) ***\n", count);
        LogPrintf("*** Install libsodium and rebuild before Argon2 emergency activation! ***\n");
    }

    // Combine input with salt and hash with SHA256 (not memory-hard!)
    CSHA256 hasher;
    hasher.Write(data, len);
    hasher.Write(salt.begin(), 32);
    // Add parameters to make it deterministic based on config
    hasher.Write(reinterpret_cast<const unsigned char*>(&m_memory_cost), sizeof(m_memory_cost));
    hasher.Write(reinterpret_cast<const unsigned char*>(&m_time_cost), sizeof(m_time_cost));
    // AUDIT FIX [L-03]: Include m_parallelism in the hash input for consistency
    // with the real Argon2id implementation where parallelism affects the output.
    hasher.Write(reinterpret_cast<const unsigned char*>(&m_parallelism), sizeof(m_parallelism));
    hasher.Finalize(result.begin());
#endif

    return result;
}

uint256 Argon2Context::CalculateBlockHash(const CBlockHeader& header) const
{
    // Serialize block header
    DataStream ss{};
    ss << header;

    // Use hashPrevBlock as salt for Argon2
    // This ensures each block has a unique salt, preventing precomputation
    return CalculateHash(
        reinterpret_cast<const unsigned char*>(ss.data()),
        ss.size(),
        header.hashPrevBlock
    );
}

bool Argon2Context::IsInitialized() const
{
    LOCK(m_mutex);
    return m_initialized;
}

void InitArgon2Context(uint32_t memory_cost, uint32_t time_cost, uint32_t parallelism)
{
    // SECURITY FIX [M-01]: Use std::call_once to prevent data race on g_argon2_context.
    // Previously, two threads could both read g_argon2_context as null and both call
    // make_unique, causing undefined behavior on the non-atomic unique_ptr assignment.
    //
    // SECURITY FIX [M-11]: Capture parameters by VALUE, not by reference.
    // With [&], if the first call wins the race with different parameters
    // (e.g., from test code), all subsequent calls silently use wrong values,
    // causing consensus divergence. Capturing by value freezes the parameters
    // at the point of the winning call.
    static std::once_flag g_argon2_init_flag;
    std::call_once(g_argon2_init_flag, [memory_cost, time_cost, parallelism]() {
#if !USE_LIBSODIUM
        LogPrintf("WARNING: Argon2 context using weak SHA256 fallback (libsodium not available)\n");
#endif
        g_argon2_context = std::make_unique<Argon2Context>(
            memory_cost, time_cost, parallelism);
    });
}

uint256 CalculateArgon2Hash(const CBlockHeader& header, const Consensus::Params& params)
{
    // InitArgon2Context is thread-safe via std::call_once
    InitArgon2Context(
        params.nArgon2MemoryCost,
        params.nArgon2TimeCost,
        params.nArgon2Parallelism
    );

    return g_argon2_context->CalculateBlockHash(header);
}
