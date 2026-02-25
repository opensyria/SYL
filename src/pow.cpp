// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Copyright (c) 2025-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// OpenSY: Forked from Bitcoin Core with dual-phase proof-of-work.

// ═══════════════════════════════════════════════════════════════════════════
// OPENSY TWO-PHASE PROOF-OF-WORK SYSTEM
// ═══════════════════════════════════════════════════════════════════════════
//
// PHASE 1: SHA256d (Blocks 0 - 209,999)
//   Purpose: Chain bootstrapping and initial distribution (10% of total supply)
//   Algorithm: Standard Bitcoin SHA256 double-hash
//   Rationale: Proven algorithm for initial chain establishment
//
// PHASE 2: RandomX (Blocks 210,000+)
//   Purpose: Community mining phase (90% of total supply)
//   Algorithm: RandomX - CPU-optimized, ASIC-resistant
//   Rationale: Democratizes mining for Syrian community without specialized hardware
//
// SECURITY ADVANTAGES OF THIS APPROACH:
// - Phase 1 establishes strong chainwork for assumevalid/checkpoints
// - Phase 2 prevents ASIC/GPU domination of community mining
// - No vulnerability to Bitcoin hashrate redirection in Phase 2
//
// RANDOMX CONSIDERATIONS (Phase 2):
// - Validation is slower than SHA256d (~100x) but acceptable for 2-min blocks
// - Key rotation every 32 blocks (mainnet) prevents pre-computation attacks
// - Light mode (256KB) for validation, full mode (2GB) for mining
//
// ARGON2ID EMERGENCY FALLBACK:
// If RandomX is compromised (cryptographic break, critical vulnerability),
// the network can activate Argon2id as an emergency fallback via hard fork.
// ═══════════════════════════════════════════════════════════════════════════

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <crypto/randomx_context.h>
#include <crypto/randomx_pool.h>
#include <crypto/argon2_context.h>
#include <logging.h>
#include <primitives/block.h>
#include <streams.h>
#include <sync.h>
#include <uint256.h>
#include <util/check.h>

#include <mutex>

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    
    // Use different powLimit based on whether we're in RandomX territory
    int nextHeight = pindexLast->nHeight + 1;
    const uint256& activePowLimit = params.GetRandomXPowLimit(nextHeight);
    unsigned int nProofOfWorkLimit = UintToArith256(activePowLimit).GetCompact();

    // NO RETARGETING: When fPowNoRetargeting is true, always use minimum difficulty
    // Used for regtest and special testing scenarios
    if (params.fPowNoRetargeting) {
        return nProofOfWorkLimit;
    }

    // GENESIS BOOTSTRAP PHASE: At the end of the bootstrap phase, reset to
    // the active algorithm's minimum difficulty to begin normal adjustment.
    // Blocks within the bootstrap phase used relaxed difficulty (powLimitBootstrap).
    if (params.nBootstrapEndHeight >= 0 && nextHeight == params.nBootstrapEndHeight + 1) {
        return nProofOfWorkLimit;
    }

    // At the RandomX fork height, reset to minimum difficulty for the new algorithm
    if (nextHeight == params.nRandomXForkHeight) {
        return nProofOfWorkLimit;
    }

    // At the Argon2 emergency height, reset to minimum difficulty for the fallback algorithm
    // This ensures mining can proceed immediately if RandomX is ever compromised
    if (params.nArgon2EmergencyHeight >= 0 && nextHeight == params.nArgon2EmergencyHeight) {
        return nProofOfWorkLimit;
    }

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2× nPowTargetSpacing
            // then it MUST be a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                // SECURITY FIX [M-13]: Stop the difficulty walk at the PoW algorithm
                // boundary. Without this guard, the walk can cross from RandomX blocks
                // back into SHA256d blocks, returning an nBits value for the wrong
                // algorithm. This would produce a target that is either impossibly hard
                // or trivially easy for the active algorithm, causing a chain stall or
                // allowing zero-work blocks on testnet.
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit) {
                    // Don't walk past the fork boundary into a different PoW algorithm
                    if (params.GetPowAlgorithm(pindex->pprev->nHeight) != params.GetPowAlgorithm(nextHeight)) {
                        break;
                    }
                    pindex = pindex->pprev;
                }
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Use appropriate powLimit based on height (SHA256d vs RandomX)
    int nextHeight = pindexLast->nHeight + 1;
    const arith_uint256 bnPowLimit = UintToArith256(params.GetRandomXPowLimit(nextHeight));
    arith_uint256 bnNew;

    // Normal difficulty adjustment for RandomX blocks

    // Special difficulty rule for Testnet4
    if (params.enforce_BIP94) {
        // Here we use the first block of the difficulty period. This way
        // the real difficulty is always preserved in the first block as
        // it is not allowed to use the min-difficulty exception.
        int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
        const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);

        // SECURITY FIX [C-01]: Guard against cross-algorithm difficulty base.
        // If the first block of the difficulty period was mined under a different
        // PoW algorithm (e.g., SHA256d block used as base for RandomX retarget),
        // the nBits would be incompatible — potentially 256x too hard — causing
        // a chain stall.
        //
        // SECURITY FIX [C-01b]: Additionally, when the epoch spans a PoW fork
        // boundary, the timespan-based adjustment MUST be skipped entirely.
        // RandomX's powLimitRandomX is a 240-bit number; multiplying it by
        // nActualTimespan (up to 22 bits) produces a 262-bit product that
        // OVERFLOWS arith_uint256 (256 bits), yielding a garbage target ~40x
        // too hard. This would stall the chain at the first retarget after fork.
        // Instead, reset to minimum difficulty — the next full-algorithm epoch
        // will perform a proper timespan-based adjustment.
        if (params.GetPowAlgorithm(nHeightFirst) == params.GetPowAlgorithm(nextHeight)) {
            bnNew.SetCompact(pindexFirst->nBits);
        } else {
            LogPrintf("PoW: Cross-algorithm retarget at height %d — resetting to powLimit (algo mismatch: %s vs %s, skipping timespan adjustment to avoid arith_uint256 overflow)\n",
                      nextHeight, GetPowAlgorithmName(nHeightFirst, params), GetPowAlgorithmName(nextHeight, params));
            return bnPowLimit.GetCompact();
        }
    } else {
        bnNew.SetCompact(pindexLast->nBits);
    }

    // SECURITY FIX [C-01c]: Overflow-safe difficulty adjustment for large targets.
    //
    // Bitcoin's SHA256d powLimit is ~2^208 (compact 0x1d00ffff). Multiplying by
    // nActualTimespan (max ~2^22) gives ~2^230, safely within arith_uint256's
    // 256-bit capacity.
    //
    // OpenSY's RandomX powLimitRandomX is ~2^240 (compact 0x1f00ffff). The same
    // multiplication gives ~2^262, which OVERFLOWS arith_uint256 silently
    // (operator*= discards the carry), producing a garbage target ~40x too hard.
    //
    // Fix: Divide by nPowTargetTimespan FIRST, then multiply by nActualTimespan.
    // The quotient is at most ~2^240 / ~2^20 = ~2^220. Multiplied by uint32_t
    // nActualTimespan (~2^22 max), the product is ~2^242 — within 256 bits.
    // The remainder term adds precision lost by integer division.
    {
        // nActualTimespan is clamped to [T/4, 4T] above, always fits uint32_t
        uint32_t ts = static_cast<uint32_t>(nActualTimespan);
        uint32_t tgt = static_cast<uint32_t>(params.nPowTargetTimespan);
        // Compute: bnNew = bnNew * ts / tgt via divide-first
        arith_uint256 quotient = bnNew / arith_uint256(tgt);
        arith_uint256 remainder = bnNew - quotient * tgt;
        bnNew = quotient * ts + remainder * ts / arith_uint256(tgt);
    }

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

// Check that on difficulty adjustments, the new difficulty does not increase
// or decrease beyond the permitted limits.
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits)
{
    if (params.fPowAllowMinDifficultyBlocks) return true;

    // At RandomX fork height, difficulty resets to minimum - this is always permitted
    if (height == params.nRandomXForkHeight) {
        return true;
    }

    // At Argon2 emergency height, difficulty resets to minimum - this is always permitted
    if (params.nArgon2EmergencyHeight >= 0 && height == params.nArgon2EmergencyHeight) {
        return true;
    }

    if (height % params.DifficultyAdjustmentInterval() == 0) {
        // SECURITY FIX [C-01b]: At cross-algorithm retarget boundaries (e.g., the
        // first retarget after the RandomX fork), CalculateNextWorkRequired returns
        // powLimit directly. The new_nbits == powLimit transition must be accepted.
        // The timespan-based range check below would OVERFLOW arith_uint256 for
        // RandomX's 240-bit powLimit, producing garbage bounds. Skip the range
        // check and accept powLimit as valid when old_nbits is also at powLimit.
        const arith_uint256 pow_limit = UintToArith256(params.GetActivePowLimit(height));
        arith_uint256 observed_new_target;
        observed_new_target.SetCompact(new_nbits);

        // If the new target is at or below powLimit, it's always acceptable at
        // a cross-algorithm boundary. Check if old_nbits is at powLimit (meaning
        // we're in a minimum-difficulty epoch, typical after a PoW fork).
        arith_uint256 old_target;
        old_target.SetCompact(old_nbits);
        if (old_target == pow_limit && observed_new_target == pow_limit) {
            return true;
        }

        int64_t smallest_timespan = params.nPowTargetTimespan/4;
        int64_t largest_timespan = params.nPowTargetTimespan*4;

        // SECURITY FIX [H-05]: Use height-aware powLimit for the correct algorithm.
        // Previously used params.powLimit (SHA256d) unconditionally, which rejected
        // legitimate RandomX difficulty transitions (RandomX powLimit is 256x higher).
        //
        // SECURITY FIX [C-01c]: Use overflow-safe divide-first multiplication.
        // See CalculateNextWorkRequired for full explanation of the overflow issue.

        // Calculate the largest difficulty value possible (overflow-safe):
        arith_uint256 largest_difficulty_target;
        largest_difficulty_target.SetCompact(old_nbits);
        {
            uint32_t tgt = static_cast<uint32_t>(params.nPowTargetTimespan);
            uint32_t ts = static_cast<uint32_t>(largest_timespan);
            arith_uint256 q = largest_difficulty_target / arith_uint256(tgt);
            arith_uint256 r = largest_difficulty_target - q * tgt;
            largest_difficulty_target = q * ts + r * ts / arith_uint256(tgt);
        }

        if (largest_difficulty_target > pow_limit) {
            largest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 maximum_new_target;
        maximum_new_target.SetCompact(largest_difficulty_target.GetCompact());
        if (maximum_new_target < observed_new_target) return false;

        // Calculate the smallest difficulty value possible (overflow-safe):
        arith_uint256 smallest_difficulty_target;
        smallest_difficulty_target.SetCompact(old_nbits);
        {
            uint32_t tgt = static_cast<uint32_t>(params.nPowTargetTimespan);
            uint32_t ts = static_cast<uint32_t>(smallest_timespan);
            arith_uint256 q = smallest_difficulty_target / arith_uint256(tgt);
            arith_uint256 r = smallest_difficulty_target - q * tgt;
            smallest_difficulty_target = q * ts + r * ts / arith_uint256(tgt);
        }

        if (smallest_difficulty_target > pow_limit) {
            smallest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 minimum_new_target;
        minimum_new_target.SetCompact(smallest_difficulty_target.GetCompact());
        if (minimum_new_target > observed_new_target) return false;
    } else if (old_nbits != new_nbits) {
        // PHASE 1 MIN-DIFFICULTY MINING COMPATIBILITY
        //
        // The Phase 1 bootstrap chain (blocks 0–209,999) was mined with minimum
        // difficulty to accelerate initial supply distribution. This means nBits
        // can change at non-retarget boundaries (e.g., block 44,321 resets to
        // powLimit mid-epoch) and retarget boundaries compute different values
        // than the DAA would predict. Both are legitimate on the live chain.
        //
        // Phase 2 (RandomX, blocks 210,000+) uses standard DAA rules and must
        // enforce strict nBits consistency between retarget boundaries,
        // EXCEPT during the genesis bootstrap phase where relaxed difficulty
        // was used (blocks up to nBootstrapEndHeight).
        bool isBootstrap = params.nBootstrapEndHeight >= 0 && height <= params.nBootstrapEndHeight;
        if (!params.IsRandomXActive(height) || isBootstrap) {
            return true;
        }
        return false;
    }
    return true;
}

// Bypasses the actual proof of work check during fuzz testing with a simplified validation checking whether
// the most significant bit of the last byte of the hash is set.
// AUDIT FIX [v4 ISSUE-010]: Take hash by const reference.
bool CheckProofOfWork(const uint256& hash, unsigned int nBits, const Consensus::Params& params)
{
    if (EnableFuzzDeterminism()) return (hash.data()[31] & 0x80) == 0;
    return CheckProofOfWorkImpl(hash, nBits, params);
}

std::optional<arith_uint256> DeriveTarget(unsigned int nBits, const uint256& pow_limit)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(pow_limit))
        return {};

    return bnTarget;
}

// AUDIT FIX [v4 ISSUE-010]: Take hash by const reference.
bool CheckProofOfWorkImpl(const uint256& hash, unsigned int nBits, const Consensus::Params& params)
{
    // Try normal powLimit first
    auto bnTarget{DeriveTarget(nBits, params.powLimit)};
    if (!bnTarget) {
        // Genesis bootstrap phase: blocks may have nBits exceeding normal powLimit
        if (!params.powLimitBootstrap.IsNull()) {
            bnTarget = DeriveTarget(nBits, params.powLimitBootstrap);
        }
        if (!bnTarget) return false;
    }

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}

// Height-aware version that uses appropriate powLimit for SHA256d vs RandomX
// AUDIT FIX [v4 ISSUE-010]: Take hash by const reference.
bool CheckProofOfWorkImpl(const uint256& hash, unsigned int nBits, int height, const Consensus::Params& params)
{
    // Use bootstrap powLimit for blocks in the genesis bootstrap phase
    const uint256* activePowLimitPtr;
    if (params.nBootstrapEndHeight >= 0 && height >= 0 && height <= params.nBootstrapEndHeight) {
        activePowLimitPtr = &params.powLimitBootstrap;
    } else {
        activePowLimitPtr = &params.GetRandomXPowLimit(height);
    }
    auto bnTarget{DeriveTarget(nBits, *activePowLimitPtr)};
    if (!bnTarget) return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}

// =============================================================================
// RANDOMX PROOF-OF-WORK FUNCTIONS
// =============================================================================

uint256 GetRandomXKeyBlockHash(int height, const CBlockIndex* pindex, const Consensus::Params& params)
{
    int keyHeight = params.GetRandomXKeyBlockHeight(height);

    // For early blocks (before we have enough history), use genesis
    if (keyHeight < 0) {
        keyHeight = 0;
    }

    // Traverse back to the key block
    const CBlockIndex* keyBlock = pindex;
    while (keyBlock && keyBlock->nHeight > keyHeight) {
        keyBlock = keyBlock->pprev;
    }

    // If we couldn't find the key block, log and return empty hash
    if (!keyBlock || keyBlock->nHeight != keyHeight) {
        // SECURITY FIX [M-16]: Upgraded from LogDebug to LogPrintf. A null key block
        // hash means RandomX blocks at this height CANNOT be validated, which is a
        // critical consensus issue — not a debug-level event.
        LogPrintf("GetRandomXKeyBlockHash: CRITICAL - Failed to find key block at height %d for block height %d (pindex=%s, keyBlock=%s). "
                  "RandomX blocks at this height will be rejected.\n",
                 keyHeight, height,
                 pindex ? std::to_string(pindex->nHeight) : "null",
                 keyBlock ? std::to_string(keyBlock->nHeight) : "null");
        return uint256();
    }

    return keyBlock->GetBlockHash();
}

// =============================================================================
// RANDOMX CONTEXT POOL
// =============================================================================
//
// SECURITY FIX [H-01]: Thread-Local RandomX Context Memory Accumulation
//
// Previously, each thread had its own thread_local RandomX context (~256KB each),
// leading to unbounded memory growth under high concurrency (many RPC requests,
// parallel block validation).
//
// The new pooled approach:
// 1. Limits total contexts to MAX_CONTEXTS (default 8) = 2MB max memory
// 2. Uses RAII guards for automatic checkout/checkin
// 3. Implements key-aware context reuse (avoids re-initialization)
// 4. Blocks threads when pool is exhausted (bounded memory)
//
// This prevents memory exhaustion attacks where an adversary could cause
// unbounded thread creation to consume all available memory.
// =============================================================================

uint256 CalculateRandomXHash(const CBlockHeader& header, const uint256& keyBlockHash)
{
    // Acquire a context from the global pool with CONSENSUS_CRITICAL priority
    // This ensures block validation never fails due to pool exhaustion
    auto guard = g_randomx_pool.Acquire(keyBlockHash, AcquisitionPriority::CONSENSUS_CRITICAL);
    if (!guard.has_value()) {
        // FIX M-01: This should NEVER happen with CONSENSUS_CRITICAL priority
        // which has infinite timeout and highest preemption rights.
        // If this occurs, something is fundamentally broken.
        LogPrintf("RandomX: CRITICAL - Failed to acquire context from pool. This indicates a bug in the pool implementation.\n");
        
        // Assert in debug builds to catch this during testing
        assert(!"CONSENSUS_CRITICAL RandomX context acquisition failed - this should be impossible");
        
        // In release builds, return max hash which will fail validation
        // This is safer than proceeding with undefined behavior
        return uint256{"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    }

    // Serialize block header
    DataStream ss{};
    ss << header;

    // Calculate and return RandomX hash
    // Context is automatically returned to pool when guard destructs
    return (*guard)->CalculateHash(
        reinterpret_cast<const unsigned char*>(ss.data()), ss.size());
}

// =============================================================================
// ALGORITHM NAME HELPER
// =============================================================================

const char* GetPowAlgorithmName(int height, const Consensus::Params& params)
{
    switch (params.GetPowAlgorithm(height)) {
        case Consensus::Params::PowAlgorithm::ARGON2ID:
            return "Argon2id";
        case Consensus::Params::PowAlgorithm::RANDOMX:
            return "RandomX";
        case Consensus::Params::PowAlgorithm::SHA256D:
        default:
            return "SHA256d";
    }
}

// =============================================================================
// UNIFIED PROOF-OF-WORK VALIDATION
// =============================================================================

bool CheckProofOfWorkAtHeight(const CBlockHeader& header, int height, const CBlockIndex* pindex, const Consensus::Params& params)
{
    // Determine which PoW algorithm to use based on height and consensus rules
    const auto algorithm = params.GetPowAlgorithm(height);

    switch (algorithm) {
        case Consensus::Params::PowAlgorithm::ARGON2ID: {
            // Argon2id emergency fallback - only activated if RandomX is compromised
            LogPrintf("PoW: Using Argon2id emergency fallback at height %d\n", height);

            uint256 argon2Hash = CalculateArgon2Hash(header, params);
            return CheckProofOfWorkImpl(argon2Hash, header.nBits, height, params);
        }

        case Consensus::Params::PowAlgorithm::RANDOMX: {
            // RandomX proof-of-work for blocks at or after fork height
            uint256 keyBlockHash = GetRandomXKeyBlockHash(height, pindex, params);
            if (keyBlockHash.IsNull()) {
                // Can't determine key block - reject
                return false;
            }

            uint256 randomxHash = CalculateRandomXHash(header, keyBlockHash);
            return CheckProofOfWorkImpl(randomxHash, header.nBits, height, params);
        }

        case Consensus::Params::PowAlgorithm::SHA256D:
        default: {
            // SHA256d proof-of-work for genesis/legacy blocks
            return CheckProofOfWork(header.GetHash(), header.nBits, params);
        }
    }
}

bool CheckProofOfWorkForBlockIndex(const CBlockHeader& header, int height, const Consensus::Params& params)
{
    // ==========================================================================
    // SECURITY DOCUMENTATION [H-07]: CheckProofOfWorkForBlockIndex
    // ==========================================================================
    //
    // This function INTENTIONALLY performs WEAK validation for RandomX/Argon2id
    // blocks. Only the nBits range is checked — NOT the actual PoW hash.
    //
    // Full PoW hash validation occurs in:
    //   - CheckProofOfWorkAtHeight() during initial block acceptance
    //   - ContextualCheckBlockHeader() during chain activation
    //   - AcceptBlock() / ConnectBlock() before any chain state changes
    //
    // WHY THIS IS ACCEPTABLE:
    //   1. This function is called during index loading from disk, where blocks
    //      were ALREADY fully validated when first accepted.
    //   2. During index loading, blocks arrive in arbitrary order and pprev
    //      pointers may not be set, so we CANNOT traverse the chain to find
    //      the RandomX key block hash needed for full PoW computation.
    //   3. An attacker with disk write access has already compromised the node
    //      (disk = trusted storage model).
    //
    // RISK ASSESSMENT:
    //   - An attacker who can modify blk*.dat files on disk could insert blocks
    //     with valid nBits but invalid RandomX hashes. These would pass the
    //     index load but would be caught during chain activation when full
    //     PoW is verified via CheckProofOfWorkAtHeight().
    //   - The window of vulnerability is between index load and chain activation,
    //     during which the invalid block may appear in the block index but cannot
    //     become part of the active chain.
    //
    // IMPORTANT: Do NOT rely on this function alone for consensus security.
    // Any code path that leads to chain state changes MUST use
    // CheckProofOfWorkAtHeight() instead.
    // ==========================================================================

    const auto algorithm = params.GetPowAlgorithm(height);

    switch (algorithm) {
        case Consensus::Params::PowAlgorithm::ARGON2ID:
        case Consensus::Params::PowAlgorithm::RANDOMX: {
            // For memory-hard algorithms during index load: just verify nBits is valid
            // Use bootstrap powLimit for blocks in the genesis bootstrap phase
            const uint256& activePowLimit = (params.nBootstrapEndHeight >= 0 && height <= params.nBootstrapEndHeight)
                ? params.powLimitBootstrap
                : params.GetActivePowLimit(height);
            auto bnTarget = DeriveTarget(header.nBits, activePowLimit);
            return bnTarget.has_value();  // Valid if nBits parses to a valid target within powLimit
        }

        case Consensus::Params::PowAlgorithm::SHA256D:
        default: {
            // SHA256d blocks: use bootstrap powLimit if in bootstrap phase
            if (params.nBootstrapEndHeight >= 0 && height <= params.nBootstrapEndHeight) {
                auto bnTarget = DeriveTarget(header.nBits, params.powLimitBootstrap);
                if (!bnTarget) return false;
                return UintToArith256(header.GetHash()) <= *bnTarget;
            }
            return CheckProofOfWork(header.GetHash(), header.nBits, params);
        }
    }
}
