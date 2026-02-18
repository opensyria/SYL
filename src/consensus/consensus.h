// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENSY_CONSENSUS_CONSENSUS_H
#define OPENSY_CONSENSUS_CONSENSUS_H

#include <cstdint>
#include <cstdlib>

/** The maximum allowed size for a serialized block, in bytes (only for buffer size limits) */
static const unsigned int MAX_BLOCK_SERIALIZED_SIZE = 4000000;
/** The maximum allowed weight for a block, see BIP 141 (network rule) */
static const unsigned int MAX_BLOCK_WEIGHT = 4000000;
/** The maximum allowed number of signature check operations in a block (network rule) */
static const int64_t MAX_BLOCK_SIGOPS_COST = 80000;
/** Coinbase transaction outputs can only be spent after this number of new blocks (network rule) */
static const int COINBASE_MATURITY = 100;
// NOTE: With 2-minute blocks, 100 blocks = ~3.3 hours maturity time
// (vs Bitcoin's ~16.7 hours at 10-minute blocks).
// AUDIT REVIEW [M-MATURITY]: This shorter maturity window means miners can spend
// coinbase rewards faster. If reorg depth exceeds 100 blocks (~3.3 hours),
// spent coinbase outputs become invalid. Consider increasing to 500 (~16.7 hours)
// if deep reorgs are observed. Current value is kept for faster mining reward
// liquidity which benefits early network economics.
// Confirmation recommendations for services:
//   - Standard transactions: 6 confirmations (~12 min)
//   - High-value transactions: 30-60 confirmations (~1-2 hours)

static const int WITNESS_SCALE_FACTOR = 4;

static const size_t MIN_TRANSACTION_WEIGHT = WITNESS_SCALE_FACTOR * 60; // 60 is the lower bound for the size of a valid serialized CTransaction
static const size_t MIN_SERIALIZABLE_TRANSACTION_WEIGHT = WITNESS_SCALE_FACTOR * 10; // 10 is the lower bound for the size of a serialized CTransaction

/** Flags for nSequence and nLockTime locks */
/** Interpret sequence numbers as relative lock-time constraints. */
static constexpr unsigned int LOCKTIME_VERIFY_SEQUENCE = (1 << 0);

/**
 * Maximum number of seconds that the timestamp of the first
 * block of a difficulty adjustment period is allowed to
 * be earlier than the last block of the previous period (BIP94).
 *
 * SECURITY FIX [H-01]: Reduced from 600 to 120 for OpenSY's 2-minute blocks.
 * Bitcoin uses 600s (= 1 block at 10-min intervals). Proportionally,
 * OpenSY's 2-minute blocks require 120s to maintain the same 1-block tolerance.
 * 600s on a 2-min chain allows a 5-block timewarp, which is excessive.
 *
 * DEPLOYMENT NOTE: This is a consensus rule change that requires a coordinated
 * network upgrade (hard fork). All nodes must upgrade before activation height.
 */
static constexpr int64_t MAX_TIMEWARP = 120;

#endif // OPENSY_CONSENSUS_CONSENSUS_H
