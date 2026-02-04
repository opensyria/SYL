// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENSY_POW_H
#define OPENSY_POW_H

#include <consensus/params.h>

#include <cstdint>
#include <vector>

class CBlockHeader;
class CBlockIndex;
class uint256;
class arith_uint256;

/**
 * Convert nBits value to target.
 *
 * @param[in] nBits     compact representation of the target
 * @param[in] pow_limit PoW limit (consensus parameter)
 *
 * @return              the proof-of-work target or nullopt if the nBits value
 *                      is invalid (due to overflow or exceeding pow_limit)
 */
std::optional<arith_uint256> DeriveTarget(unsigned int nBits, const uint256 pow_limit);

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params&);
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params&);

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits (SHA256d) */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params&);
bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params&);
/** Height-aware version that uses appropriate powLimit for SHA256d vs RandomX */
bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, int height, const Consensus::Params&);

/**
 * Check proof-of-work for a block at a specific height.
 * Uses RandomX for blocks at or after fork height, SHA256d for earlier blocks.
 * Falls back to Argon2id if emergency fallback is activated.
 *
 * @param[in] header    Block header to validate
 * @param[in] height    Height of the block (determines which algorithm to use)
 * @param[in] pindex    Block index for accessing the chain (to get key block hash)
 * @param[in] params    Consensus parameters
 * @return true if proof-of-work is valid
 */
bool CheckProofOfWorkAtHeight(const CBlockHeader& header, int height, const CBlockIndex* pindex, const Consensus::Params& params);

/**
 * Get the name of the PoW algorithm active at a given height.
 * Useful for logging and debugging.
 *
 * @param[in] height    Block height
 * @param[in] params    Consensus parameters
 * @return String name of the algorithm ("SHA256d", "RandomX", or "Argon2id")
 */
const char* GetPowAlgorithmName(int height, const Consensus::Params& params);

/**
 * Simplified proof-of-work check for block index loading.
 * During index loading, blocks are loaded in arbitrary order and pprev pointers
 * may not be fully set, so we can't traverse the chain to compute RandomX hashes.
 *
 * For RandomX blocks: only validates that nBits is within the valid range.
 * For SHA256d blocks: performs full validation (no chain traversal needed).
 *
 * The actual RandomX hash verification happens during chain activation.
 *
 * @param[in] header    Block header to validate
 * @param[in] height    Height of the block (determines which algorithm to use)
 * @param[in] params    Consensus parameters
 * @return true if proof-of-work passes the simplified check
 */
bool CheckProofOfWorkForBlockIndex(const CBlockHeader& header, int height, const Consensus::Params& params);

/**
 * Calculate the RandomX hash of a block header.
 *
 * @param[in] header        Block header to hash
 * @param[in] keyBlockHash  Hash of the key block (determines RandomX initialization)
 * @return RandomX hash of the block header
 */
uint256 CalculateRandomXHash(const CBlockHeader& header, const uint256& keyBlockHash);

/**
 * Get the key block hash for RandomX at a given height.
 *
 * @param[in] height    Block height to get key for
 * @param[in] pindex    Block index to traverse chain
 * @param[in] params    Consensus parameters
 * @return Hash of the key block, or uint256() if not found
 */
uint256 GetRandomXKeyBlockHash(int height, const CBlockIndex* pindex, const Consensus::Params& params);

/**
 * Return false if the proof-of-work requirement specified by new_nbits at a
 * given height is not possible, given the proof-of-work on the prior block as
 * specified by old_nbits.
 *
 * This function only checks that the new value is within a factor of 4 of the
 * old value for blocks at the difficulty adjustment interval, and otherwise
 * requires the values to be the same.
 *
 * Always returns true on networks where min difficulty blocks are allowed,
 * such as regtest/testnet.
 */
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits);

#endif // OPENSY_POW_H
