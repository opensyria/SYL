// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Copyright (c) 2025-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// OpenSY: "Qirsh" (قرش) is the smallest unit of SYL, equivalent to
// Bitcoin's "satoshi". 1 SYL = 100,000,000 qirsh.

#ifndef OPENSY_CONSENSUS_AMOUNT_H
#define OPENSY_CONSENSUS_AMOUNT_H

#include <cstdint>

/** Amount in qirsh (قرش) - smallest unit of SYL (Can be negative)
 *  Named after the historical Syrian/Arabic currency subdivision.
 *  1 SYL = 100,000,000 qirsh (equivalent to Bitcoin's satoshi) */
typedef int64_t CAmount;

/** The amount of qirsh in one SYL. */
static constexpr CAmount COIN = 100000000;

/** No amount larger than this (in qirsh) is valid.
 *
 * Note that this constant is *not* the total money supply, which in OpenSY
 * currently happens to be less than 21,000,000 SYL for various reasons, but
 * rather a sanity check. As this sanity check is used by consensus-critical
 * validation code, the exact value of the MAX_MONEY constant is consensus
 * critical; in unusual circumstances like a(nother) overflow bug that allowed
 * for the creation of coins out of thin air modification could lead to a fork.
 * */
static constexpr CAmount MAX_MONEY = 21000000000 * COIN; // 21 billion SYL
inline bool MoneyRange(const CAmount& nValue) { return (nValue >= 0 && nValue <= MAX_MONEY); }

#endif // OPENSY_CONSENSUS_AMOUNT_H
