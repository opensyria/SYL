// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Copyright (c) 2025-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENSY_CONSENSUS_PARAMS_H
#define OPENSY_CONSENSUS_PARAMS_H

#include <script/verify_flags.h>
#include <uint256.h>

#include <array>
#include <chrono>
#include <limits>
#include <map>
#include <vector>

namespace Consensus {

/**
 * A buried deployment is one where the height of the activation has been hardcoded into
 * the client implementation long after the consensus change has activated. See BIP 90.
 */
enum BuriedDeployment : int16_t {
    // buried deployments get negative values to avoid overlap with DeploymentPos
    DEPLOYMENT_HEIGHTINCB = std::numeric_limits<int16_t>::min(),
    DEPLOYMENT_CLTV,
    DEPLOYMENT_DERSIG,
    DEPLOYMENT_CSV,
    DEPLOYMENT_SEGWIT,
};
constexpr bool ValidDeployment(BuriedDeployment dep) { return dep <= DEPLOYMENT_SEGWIT; }

enum DeploymentPos : uint16_t {
    DEPLOYMENT_TESTDUMMY,
    DEPLOYMENT_TAPROOT, // Deployment of Schnorr/Taproot (BIPs 340-342)
    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in deploymentinfo.cpp
    MAX_VERSION_BITS_DEPLOYMENTS
};
constexpr bool ValidDeployment(DeploymentPos dep) { return dep < MAX_VERSION_BITS_DEPLOYMENTS; }

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit{28};
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime{NEVER_ACTIVE};
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout{NEVER_ACTIVE};
    /** If lock in occurs, delay activation until at least this block
     *  height.  Note that activation will only occur on a retarget
     *  boundary.
     */
    int min_activation_height{0};
    /** Period of blocks to check signalling in (usually retarget period, ie params.DifficultyAdjustmentInterval()) */
    uint32_t period{2016};
    /**
     * Minimum blocks including miner confirmation of the total of 2016 blocks in a retargeting period,
     * which is also used for BIP9 deployments.
     * Examples: 1916 for 95%, 1512 for testchains.
     */
    uint32_t threshold{1916};

    /** Constant for nTimeout very far in the future. */
    static constexpr int64_t NO_TIMEOUT = std::numeric_limits<int64_t>::max();

    /** Special value for nStartTime indicating that the deployment is always active.
     *  This is useful for testing, as it means tests don't need to deal with the activation
     *  process (which takes at least 3 BIP9 intervals). Only tests that specifically test the
     *  behaviour during activation cannot use this. */
    static constexpr int64_t ALWAYS_ACTIVE = -1;

    /** Special value for nStartTime indicating that the deployment is never active.
     *  This is useful for integrating the code changes for a new feature
     *  prior to deploying it on some or all networks. */
    static constexpr int64_t NEVER_ACTIVE = -2;
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    int nSubsidyHalvingInterval;
    /**
     * Hashes of blocks that
     * - are known to be consensus valid, and
     * - buried in the chain, and
     * - fail if the default script verify flags are applied.
     */
    std::map<uint256, script_verify_flags> script_flag_exceptions;
    /** Block height and hash at which BIP34 becomes active */
    int BIP34Height;
    uint256 BIP34Hash;
    /** Block height at which BIP65 becomes active */
    int BIP65Height;
    /** Block height at which BIP66 becomes active */
    int BIP66Height;
    /** Block height at which CSV (BIP68, BIP112 and BIP113) becomes active */
    int CSVHeight;
    /** Block height at which Segwit (BIP141, BIP143 and BIP147) becomes active.
     * Note that segwit v0 script rules are enforced on all blocks except the
     * BIP 16 exception blocks. */
    int SegwitHeight;
    /** Don't warn about unknown BIP 9 activations below this height.
     * This prevents us from warning about the CSV and segwit activations. */
    int MinBIP9WarningHeight;
    std::array<BIP9Deployment,MAX_VERSION_BITS_DEPLOYMENTS> vDeployments;
    /** Proof of work parameters */
    uint256 powLimit;
    bool fPowAllowMinDifficultyBlocks;
    /**
      * Enforce BIP94 timewarp attack mitigation. On testnet4 this also enforces
      * the block storm mitigation.
      */
    bool enforce_BIP94;
    bool fPowNoRetargeting;
    int64_t nPowTargetSpacing;
    int64_t nPowTargetTimespan;
    std::chrono::seconds PowTargetSpacing() const
    {
        return std::chrono::seconds{nPowTargetSpacing};
    }
    int64_t DifficultyAdjustmentInterval() const { return nPowTargetTimespan / nPowTargetSpacing; }
    /** The best chain should have at least this much work */
    uint256 nMinimumChainWork;
    /** By default assume that the signatures in ancestors of this block are valid */
    uint256 defaultAssumeValid;

    /**
     * If true, witness commitments contain a payload equal to an OpenSY Script solution
     * to the signet challenge. See BIP325.
     */
    bool signet_blocks{false};
    std::vector<uint8_t> signet_challenge;

    /**
     * RandomX Hard Fork Parameters
     *
     * OpenSY switches from SHA256d to RandomX proof-of-work at nRandomXForkHeight
     * to democratize mining and prevent ASIC/GPU domination.
     *
     * KEY ROTATION (nRandomXKeyBlockInterval = 32 blocks):
     * RandomX uses a "key" derived from a historical block hash. This key changes
     * every 32 blocks (~64 minutes with 2-minute block times). This provides:
     *   - Security: Prevents pre-computation of RandomX datasets
     *   - Fairness: All miners must recompute their datasets regularly
     *   - Anti-ASIC: Makes custom hardware less effective
     *
     * NOTE: Some documentation may reference 2048 blocks - this was an earlier
     * design. The current value of 32 provides tighter security bounds.
     */
    int nRandomXForkHeight{210000};       //!< Block height at which RandomX activates (mainnet: 210,000 = 10% of supply)
    int nRandomXKeyBlockInterval{32};     //!< How often the RandomX key changes (blocks) - 32 blocks = ~64 min
    uint256 powLimitRandomX;              //!< Minimum difficulty for RandomX blocks (resets at fork)

    /**
     * Emergency Fallback PoW Parameters (Argon2id)
     *
     * If RandomX is compromised (cryptographic break, critical vulnerability),
     * the network can activate Argon2id as an emergency CPU-friendly fallback.
     *
     * ACTIVATION: Via BIP9 signaling or emergency hard fork at nArgon2EmergencyHeight.
     * This is a dormant mechanism - only activated if RandomX becomes unsafe.
     *
     * Argon2id chosen because:
     *   - Password Hashing Competition winner (2015)
     *   - Memory-hard and ASIC-resistant
     *   - Resistant to side-channel attacks (id variant)
     *   - Widely audited (1Password, Bitwarden, Signal, Cloudflare)
     *   - Simpler than RandomX = smaller attack surface
     */
    int nArgon2EmergencyHeight{-1};       //!< Height at which Argon2id activates (-1 = never, emergency only)
    uint32_t nArgon2MemoryCost{1 << 21};  //!< Memory in KiB (2GB = 2097152 KiB, matches RandomX)
    uint32_t nArgon2TimeCost{1};          //!< Number of iterations
    uint32_t nArgon2Parallelism{1};       //!< Parallelism factor
    uint256 powLimitArgon2;               //!< Minimum difficulty for Argon2id blocks

    /** Check if RandomX proof-of-work is active at the given height */
    bool IsRandomXActive(int height) const
    {
        // RandomX is active after fork height, but NOT if Argon2 emergency is active
        return height >= nRandomXForkHeight && !IsArgon2EmergencyActive(height);
    }

    /** Check if Argon2id emergency fallback is active at the given height */
    bool IsArgon2EmergencyActive(int height) const
    {
        return nArgon2EmergencyHeight >= 0 && height >= nArgon2EmergencyHeight;
    }

    /**
     * Proof-of-Work Algorithm Enumeration
     * Used for explicit algorithm selection in validation and mining code.
     */
    enum class PowAlgorithm {
        SHA256D,    //!< Genesis block only (or pre-fork if applicable)
        RANDOMX,    //!< Primary algorithm from block 1
        ARGON2ID    //!< Emergency fallback if RandomX compromised
    };

    /** Get the active PoW algorithm for a given block height */
    PowAlgorithm GetPowAlgorithm(int height) const
    {
        if (IsArgon2EmergencyActive(height)) {
            return PowAlgorithm::ARGON2ID;
        }
        if (IsRandomXActive(height)) {
            return PowAlgorithm::RANDOMX;
        }
        return PowAlgorithm::SHA256D;
    }

    /** Get the appropriate powLimit based on block height and active algorithm */
    const uint256& GetActivePowLimit(int height) const
    {
        switch (GetPowAlgorithm(height)) {
        case PowAlgorithm::ARGON2ID:
            return powLimitArgon2.IsNull() ? powLimitRandomX : powLimitArgon2;
        case PowAlgorithm::RANDOMX:
            return powLimitRandomX.IsNull() ? powLimit : powLimitRandomX;
        case PowAlgorithm::SHA256D:
        default:
            return powLimit;
        }
    }

    /** Get the appropriate powLimit based on block height (SHA256d vs RandomX) */
    const uint256& GetRandomXPowLimit(int height) const
    {
        // Legacy function - calls GetActivePowLimit for backward compatibility
        return GetActivePowLimit(height);
    }

    /** Get the key block height for RandomX at a given block height.
     *  The key is derived from a block nRandomXKeyBlockInterval blocks before the current key interval.
     *  @param height The block height to calculate key block for
     *  @return Height of the block whose hash is used as RandomX key
     *
     *  KEY ROTATION MECHANICS:
     *  - Key rotates every nRandomXKeyBlockInterval blocks (32 on mainnet)
     *  - Key is derived from a block one interval behind the current interval
     *  - This provides ~64 minutes of key stability (32 blocks * 2 min/block)
     *
     *  MAINNET BEHAVIOR (nRandomXForkHeight = 210,000):
     *  - At fork height 210,000: keyHeight = 209,952 (plenty of chain history)
     *  - First key rotation at 210,032 uses block 209,984 as key
     *  - The "genesis key" edge case ONLY applies if fork height < 64
     *  - Since mainnet fork is at 210,000, key derivation always has full history
     *
     *  REGTEST/EARLY FORK NOTE:
     *  If nRandomXForkHeight < 64 (only possible in regtest), early blocks
     *  would share genesis as their key block until height >= 2*interval.
     */
    int GetRandomXKeyBlockHeight(int height) const
    {
        // Key changes every nRandomXKeyBlockInterval blocks
        // Key for height H is block at: (H / interval) * interval - interval
        //
        // Examples with interval=32 and fork at 210,000:
        //   height 210000: keyHeight = 6562*32 - 32 = 209952
        //   height 210032: keyHeight = 6563*32 - 32 = 209984
        //   height 210064: keyHeight = 6564*32 - 32 = 210016
        //
        // The clamping to 0 only matters if (height / interval) < 1,
        // which cannot happen when fork height >= interval.
        int keyHeight = (height / nRandomXKeyBlockInterval) * nRandomXKeyBlockInterval - nRandomXKeyBlockInterval;
        return keyHeight >= 0 ? keyHeight : 0;
    }

    int DeploymentHeight(BuriedDeployment dep) const
    {
        switch (dep) {
        case DEPLOYMENT_HEIGHTINCB:
            return BIP34Height;
        case DEPLOYMENT_CLTV:
            return BIP65Height;
        case DEPLOYMENT_DERSIG:
            return BIP66Height;
        case DEPLOYMENT_CSV:
            return CSVHeight;
        case DEPLOYMENT_SEGWIT:
            return SegwitHeight;
        } // no default case, so the compiler can warn about missing cases
        return std::numeric_limits<int>::max();
    }
};

} // namespace Consensus

#endif // OPENSY_CONSENSUS_PARAMS_H
