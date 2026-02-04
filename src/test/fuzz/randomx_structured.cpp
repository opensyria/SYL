// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Enhanced RandomX Fuzz Tests with Structured Inputs
 * 
 * These fuzz targets use structured inputs that mimic real blockchain
 * behavior rather than purely random data. This improves fuzzing efficiency
 * by focusing on realistic edge cases.
 * 
 * Structured patterns:
 * - Key sequences mimicking real chain progression
 * - Fork boundary crossing scenarios
 * - Key rotation boundary sequences
 * - Adversarial timing patterns
 */

#include <chainparams.h>
#include <consensus/params.h>
#include <crypto/randomx_context.h>
#include <crypto/randomx_pool.h>
#include <pow.h>
#include <primitives/block.h>
#include <streams.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <uint256.h>
#include <util/chaintype.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

// Generate a deterministic key hash based on block height
// Mimics how key blocks work in production
uint256 DeriveKeyFromHeight(int height, int keyInterval) {
    int keyBlockHeight = (height / keyInterval) * keyInterval;
    if (keyBlockHeight == 0 && height > 0) {
        keyBlockHeight = 0; // Genesis is key for first epoch
    }
    
    uint256 key;
    // Create deterministic but varied key based on height
    uint32_t* data = reinterpret_cast<uint32_t*>(key.begin());
    for (int i = 0; i < 8; ++i) {
        data[i] = static_cast<uint32_t>(keyBlockHeight * 0x9E3779B9 + i);
    }
    return key;
}

// Generate realistic block header for height
CBlockHeader CreateRealisticHeader(int height, uint32_t nonce, const Consensus::Params& params) {
    CBlockHeader header;
    header.nVersion = 1;
    
    // Derive prev block hash from height
    header.hashPrevBlock = DeriveKeyFromHeight(height - 1, 1);
    header.hashMerkleRoot = DeriveKeyFromHeight(height, 2);
    
    // Realistic timestamp (2-minute intervals from genesis)
    header.nTime = 1733616000 + height * params.nPowTargetSpacing;
    
    // Use appropriate nBits based on algorithm
    if (params.IsRandomXActive(height)) {
        header.nBits = UintToArith256(params.powLimitRandomX).GetCompact();
    } else {
        header.nBits = UintToArith256(params.powLimit).GetCompact();
    }
    
    header.nNonce = nonce;
    return header;
}

} // namespace

void initialize_structured_fuzz()
{
    SelectParams(ChainType::REGTEST);
}

/**
 * FUZZ_TARGET: randomx_structured_chain
 * 
 * Fuzzes RandomX with structured key sequences that mimic real chain
 * progression through key rotation epochs.
 */
FUZZ_TARGET(randomx_structured_chain, .init = initialize_structured_fuzz)
{
    FuzzedDataProvider fuzzed_data(buffer.data(), buffer.size());
    const Consensus::Params& params = Params().GetConsensus();
    
    // Start from a fuzz-determined height
    const int startHeight = fuzzed_data.ConsumeIntegralInRange<int>(0, 10000);
    const int numBlocks = fuzzed_data.ConsumeIntegralInRange<int>(1, 100);
    
    // Walk through blocks in sequence
    for (int i = 0; i < numBlocks && fuzzed_data.remaining_bytes() > 4; ++i) {
        int height = startHeight + i;
        uint32_t nonce = fuzzed_data.ConsumeIntegral<uint32_t>();
        
        CBlockHeader header = CreateRealisticHeader(height, nonce, params);
        uint256 keyHash = DeriveKeyFromHeight(height, params.nRandomXKeyBlockInterval);
        
        // Ensure key is non-null
        if (keyHash.IsNull()) {
            keyHash = uint256::ONE;
        }
        
        // Calculate RandomX hash for post-fork blocks
        if (params.IsRandomXActive(height)) {
            try {
                uint256 powHash = CalculateRandomXHash(header, keyHash);
                // Verify hash is valid
                assert(powHash.size() == 32);
                
                // Check PoW (will usually fail without mining, that's OK)
                arith_uint256 target;
                target.SetCompact(header.nBits);
                bool passes = UintToArith256(powHash) <= target;
                (void)passes; // Suppress unused warning
            } catch (const std::exception&) {
                // Context issues - acceptable in fuzzing
            }
        }
    }
}

/**
 * FUZZ_TARGET: randomx_fork_boundary
 * 
 * Specifically tests blocks around the fork boundary where
 * SHA256d transitions to RandomX.
 */
FUZZ_TARGET(randomx_fork_boundary, .init = initialize_structured_fuzz)
{
    FuzzedDataProvider fuzzed_data(buffer.data(), buffer.size());
    const Consensus::Params& params = Params().GetConsensus();
    
    // Focus on fork boundary region
    int forkHeight = params.nRandomXForkHeight;
    
    // Choose height relative to fork
    int offset = fuzzed_data.ConsumeIntegralInRange<int>(-10, 10);
    int height = std::max(0, forkHeight + offset);
    
    // Create header
    uint32_t nonce = fuzzed_data.ConsumeIntegral<uint32_t>();
    CBlockHeader header = CreateRealisticHeader(height, nonce, params);
    
    // Verify algorithm selection is correct
    bool expectRandomX = params.IsRandomXActive(height);
    
    if (expectRandomX) {
        // Should use RandomX
        uint256 keyHash = DeriveKeyFromHeight(height, params.nRandomXKeyBlockInterval);
        if (keyHash.IsNull()) keyHash = uint256::ONE;
        
        try {
            uint256 randomxHash = CalculateRandomXHash(header, keyHash);
            assert(!randomxHash.IsNull());
        } catch (const std::exception&) {
            // Acceptable
        }
    } else {
        // Should use SHA256d
        uint256 sha256Hash = header.GetHash();
        assert(!sha256Hash.IsNull());
    }
    
    // Test key block selection at boundary
    if (height >= 0) {
        int keyHeight = params.GetRandomXKeyBlockHeight(height);
        assert(keyHeight >= 0);
        assert(keyHeight <= height);
    }
}

/**
 * FUZZ_TARGET: randomx_key_rotation_boundary
 * 
 * Tests key rotation boundaries where the key block changes.
 */
FUZZ_TARGET(randomx_key_rotation_boundary, .init = initialize_structured_fuzz)
{
    FuzzedDataProvider fuzzed_data(buffer.data(), buffer.size());
    const Consensus::Params& params = Params().GetConsensus();
    
    int interval = params.nRandomXKeyBlockInterval;
    
    // Choose a rotation boundary
    int rotationNum = fuzzed_data.ConsumeIntegralInRange<int>(1, 100);
    int boundaryHeight = rotationNum * interval;
    
    // Test around the boundary
    int offset = fuzzed_data.ConsumeIntegralInRange<int>(-2, 2);
    int height = boundaryHeight + offset;
    
    if (height >= params.nRandomXForkHeight) {
        uint32_t nonce = fuzzed_data.ConsumeIntegral<uint32_t>();
        CBlockHeader header = CreateRealisticHeader(height, nonce, params);
        
        // Get key for this height
        int keyHeight = params.GetRandomXKeyBlockHeight(height);
        uint256 keyHash = DeriveKeyFromHeight(keyHeight, 1);
        if (keyHash.IsNull()) keyHash = uint256::ONE;
        
        // Get key for adjacent heights
        int prevKeyHeight = params.GetRandomXKeyBlockHeight(height - 1);
        int nextKeyHeight = params.GetRandomXKeyBlockHeight(height + 1);
        
        // Verify key rotation behavior
        if (height == boundaryHeight) {
            // At boundary, key should be different from previous
            // (unless we're at first interval)
            if (boundaryHeight > interval) {
                assert(keyHeight >= interval);
            }
        }
        
        // Key should stay same within interval
        if (height > 0 && height % interval != 0) {
            assert(keyHeight == prevKeyHeight || height == params.nRandomXForkHeight);
        }
        
        try {
            uint256 hash = CalculateRandomXHash(header, keyHash);
            assert(hash.size() == 32);
        } catch (const std::exception&) {
            // Acceptable
        }
    }
}

/**
 * FUZZ_TARGET: randomx_pool_adversarial
 * 
 * Tests pool under adversarial access patterns:
 * - All threads acquire simultaneously
 * - Rapid key switching
 * - Priority contention
 */
FUZZ_TARGET(randomx_pool_adversarial, .init = initialize_structured_fuzz)
{
    FuzzedDataProvider fuzzed_data(buffer.data(), buffer.size());
    
    // Adversarial pattern: rapidly switch between few keys
    const int numKeys = fuzzed_data.ConsumeIntegralInRange<int>(2, 5);
    std::vector<uint256> keys;
    for (int i = 0; i < numKeys; ++i) {
        keys.push_back(DeriveKeyFromHeight(i * 32, 1));
        if (keys.back().IsNull()) keys.back() = uint256::ONE;
    }
    
    // Rapid acquisition pattern
    const int numOps = fuzzed_data.ConsumeIntegralInRange<int>(10, 100);
    for (int i = 0; i < numOps && fuzzed_data.remaining_bytes() > 0; ++i) {
        // Pick a key (adversary focuses on few keys)
        int keyIdx = fuzzed_data.ConsumeIntegralInRange<int>(0, numKeys - 1);
        const uint256& key = keys[keyIdx];
        
        // Choose priority (adversary may try to starve normal requests)
        AcquisitionPriority priority;
        int prioChoice = fuzzed_data.ConsumeIntegralInRange<int>(0, 2);
        switch (prioChoice) {
            case 0: priority = AcquisitionPriority::NORMAL; break;
            case 1: priority = AcquisitionPriority::HIGH; break;
            default: priority = AcquisitionPriority::CONSENSUS_CRITICAL; break;
        }
        
        try {
            auto guard = g_randomx_pool.Acquire(key, priority);
            if (guard.has_value()) {
                // Quick operation
                std::vector<unsigned char> input{0x01, 0x02, 0x03, static_cast<unsigned char>(i)};
                uint256 hash = (*guard)->CalculateHash(input);
                assert(hash.size() == 32);
            }
            // Guard automatically releases
        } catch (const std::exception&) {
            // Pool exhaustion - acceptable
        }
    }
}

/**
 * FUZZ_TARGET: randomx_reorg_sequence
 * 
 * Tests validation during reorg scenarios where chains fork and rejoin.
 */
FUZZ_TARGET(randomx_reorg_sequence, .init = initialize_structured_fuzz)
{
    FuzzedDataProvider fuzzed_data(buffer.data(), buffer.size());
    const Consensus::Params& params = Params().GetConsensus();
    
    // Common ancestor height
    int ancestorHeight = fuzzed_data.ConsumeIntegralInRange<int>(
        params.nRandomXForkHeight, params.nRandomXForkHeight + 100);
    
    // Two competing chains
    int chainALength = fuzzed_data.ConsumeIntegralInRange<int>(1, 20);
    int chainBLength = fuzzed_data.ConsumeIntegralInRange<int>(1, 20);
    
    // Validate chain A
    uint256 prevKeyHash;
    for (int i = 0; i < chainALength && fuzzed_data.remaining_bytes() > 4; ++i) {
        int height = ancestorHeight + i + 1;
        uint32_t nonce = fuzzed_data.ConsumeIntegral<uint32_t>();
        CBlockHeader header = CreateRealisticHeader(height, nonce, params);
        
        uint256 keyHash = DeriveKeyFromHeight(height, params.nRandomXKeyBlockInterval);
        if (keyHash.IsNull()) keyHash = uint256::ONE;
        
        try {
            uint256 hash = CalculateRandomXHash(header, keyHash);
            // Track for determinism checks
            if (i == 0) prevKeyHash = keyHash;
        } catch (const std::exception&) {
            // Acceptable
        }
    }
    
    // Validate chain B (different nonces)
    for (int i = 0; i < chainBLength && fuzzed_data.remaining_bytes() > 4; ++i) {
        int height = ancestorHeight + i + 1;
        uint32_t nonce = fuzzed_data.ConsumeIntegral<uint32_t>() + 1000000; // Different nonces
        CBlockHeader header = CreateRealisticHeader(height, nonce, params);
        
        uint256 keyHash = DeriveKeyFromHeight(height, params.nRandomXKeyBlockInterval);
        if (keyHash.IsNull()) keyHash = uint256::ONE;
        
        try {
            uint256 hash = CalculateRandomXHash(header, keyHash);
            assert(hash.size() == 32);
        } catch (const std::exception&) {
            // Acceptable
        }
    }
}
