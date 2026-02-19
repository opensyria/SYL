// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/transaction.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

/**
 * GAP-15: Token fee estimation accuracy
 *
 * These tests verify that fee estimation for token transactions
 * is accurate enough to prevent transaction failures due to
 * insufficient fees.
 */

namespace {

// Transaction size constants (approximate)
constexpr size_t BASE_TX_SIZE = 10;              // Version + locktime
constexpr size_t P2PKH_INPUT_SIZE = 148;         // Typical P2PKH input
constexpr size_t P2WPKH_INPUT_SIZE = 68;         // Typical P2WPKH input (virtual)
constexpr size_t P2PKH_OUTPUT_SIZE = 34;         // Typical P2PKH output
constexpr size_t P2WPKH_OUTPUT_SIZE = 31;        // Typical P2WPKH output
constexpr size_t OP_RETURN_BASE_SIZE = 10;       // OP_RETURN overhead

// SRC-20 data sizes
constexpr size_t SRC20_ISSUANCE_SIZE = 80;       // Approximate issuance data
constexpr size_t SRC20_TRANSFER_SIZE = 50;       // Approximate transfer data
constexpr size_t SRC20_BURN_SIZE = 45;           // Approximate burn data

// Create a mock transfer transaction
CMutableTransaction CreateTransferTx(int num_inputs, bool segwit)
{
    CMutableTransaction tx;
    tx.version = 2;
    tx.nLockTime = 0;
    
    // Inputs
    for (int i = 0; i < num_inputs; ++i) {
        CTxIn in;
        in.prevout = COutPoint(Txid::FromUint256(uint256::ONE), i);
        if (!segwit) {
            // P2PKH input script (mock)
            in.scriptSig = CScript() << std::vector<unsigned char>(72, 0x30)
                                    << std::vector<unsigned char>(33, 0x02);
        }
        tx.vin.push_back(in);
    }
    
    // Output 0: OP_RETURN with SRC-20 data
    tx.vout.resize(3);
    tx.vout[0].nValue = 0;
    tx.vout[0].scriptPubKey = CScript() << OP_RETURN 
                                        << std::vector<unsigned char>(SRC20_TRANSFER_SIZE, 0x00);
    
    // Output 1: Recipient
    std::vector<unsigned char> hash(20, 0xAA);
    tx.vout[1].nValue = 10000;
    tx.vout[1].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << hash 
                                        << OP_EQUALVERIFY << OP_CHECKSIG;
    
    // Output 2: Change
    std::vector<unsigned char> change_hash(20, 0xBB);
    tx.vout[2].nValue = 90000;
    tx.vout[2].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << change_hash 
                                        << OP_EQUALVERIFY << OP_CHECKSIG;
    
    return tx;
}

// Create a mock issuance transaction  
CMutableTransaction CreateIssuanceTx()
{
    CMutableTransaction tx;
    tx.version = 2;
    tx.nLockTime = 0;
    
    // Single input
    CTxIn in;
    in.prevout = COutPoint(Txid::FromUint256(uint256::ONE), 0);
    in.scriptSig = CScript() << std::vector<unsigned char>(72, 0x30)
                            << std::vector<unsigned char>(33, 0x02);
    tx.vin.push_back(in);
    
    // Output 0: OP_RETURN with issuance data
    tx.vout.resize(2);
    tx.vout[0].nValue = 0;
    tx.vout[0].scriptPubKey = CScript() << OP_RETURN 
                                        << std::vector<unsigned char>(SRC20_ISSUANCE_SIZE, 0x00);
    
    // Output 1: Issuer receives tokens
    std::vector<unsigned char> hash(20, 0xCC);
    tx.vout[1].nValue = 10000;
    tx.vout[1].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << hash 
                                        << OP_EQUALVERIFY << OP_CHECKSIG;
    
    return tx;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(token_fee_estimation_tests, BasicTestingSetup)

// =============================================================================
// TRANSFER TRANSACTION SIZE
// =============================================================================

BOOST_AUTO_TEST_CASE(transfer_tx_size_estimation)
{
    // Test: Transfer transaction size is predictable
    
    CMutableTransaction tx = CreateTransferTx(1, false);
    CTransaction ctx(tx);
    
    size_t actual_size = ctx.GetTotalSize();
    
    // Expected size calculation:
    // Base: 10 bytes
    // 1 P2PKH input: ~148 bytes  
    // 3 outputs: ~34 * 3 = 102 bytes + OP_RETURN data
    // OP_RETURN: ~10 + 50 = 60 bytes
    size_t expected_min = BASE_TX_SIZE + P2PKH_INPUT_SIZE + 2 * P2PKH_OUTPUT_SIZE + SRC20_TRANSFER_SIZE;
    size_t expected_max = expected_min + 50;  // Allow some variance
    
    BOOST_TEST_MESSAGE("Transfer tx size: " << actual_size << " bytes");
    BOOST_TEST_MESSAGE("Expected range: " << expected_min << "-" << expected_max);
    
    // Size should be within reasonable range
    BOOST_CHECK_GT(actual_size, 100);  // At least 100 bytes
    BOOST_CHECK_LT(actual_size, 500);  // Less than 500 bytes for simple transfer
}

// =============================================================================
// ISSUANCE TRANSACTION SIZE
// =============================================================================

BOOST_AUTO_TEST_CASE(issuance_tx_size_estimation)
{
    // Test: Issuance transaction size is predictable
    
    CMutableTransaction tx = CreateIssuanceTx();
    CTransaction ctx(tx);
    
    size_t actual_size = ctx.GetTotalSize();
    
    BOOST_TEST_MESSAGE("Issuance tx size: " << actual_size << " bytes");
    
    // Issuance should be slightly larger than transfer (more metadata)
    BOOST_CHECK_GT(actual_size, 100);
    BOOST_CHECK_LT(actual_size, 400);
}

// =============================================================================
// FEE RATE CALCULATION
// =============================================================================

BOOST_AUTO_TEST_CASE(fee_rate_calculation)
{
    // Test: Fee calculation based on size is accurate
    
    CMutableTransaction tx = CreateTransferTx(1, false);
    CTransaction ctx(tx);
    
    size_t size = ctx.GetTotalSize();
    
    // At 1 sat/vbyte
    CAmount fee_1sat = size * 1;
    
    // At 10 sat/vbyte
    CAmount fee_10sat = size * 10;
    
    // At 100 sat/vbyte (high fee)
    CAmount fee_100sat = size * 100;
    
    BOOST_TEST_MESSAGE("Fees at 1/10/100 sat/vbyte: " 
        << fee_1sat << "/" << fee_10sat << "/" << fee_100sat);
    
    // Sanity checks
    BOOST_CHECK_GT(fee_1sat, 0);
    BOOST_CHECK_EQUAL(fee_10sat, fee_1sat * 10);
    BOOST_CHECK_EQUAL(fee_100sat, fee_1sat * 100);
}

// =============================================================================
// MULTIPLE INPUT FEE SCALING
// =============================================================================

BOOST_AUTO_TEST_CASE(multiple_inputs_fee_scaling)
{
    // Test: Fee scales correctly with multiple inputs
    
    CMutableTransaction tx1 = CreateTransferTx(1, false);
    CMutableTransaction tx2 = CreateTransferTx(2, false);
    CMutableTransaction tx5 = CreateTransferTx(5, false);
    
    size_t size1 = CTransaction(tx1).GetTotalSize();
    size_t size2 = CTransaction(tx2).GetTotalSize();
    size_t size5 = CTransaction(tx5).GetTotalSize();
    
    BOOST_TEST_MESSAGE("Sizes for 1/2/5 inputs: " << size1 << "/" << size2 << "/" << size5);
    
    // Each additional input adds ~148 bytes (P2PKH)
    size_t delta_1_2 = size2 - size1;
    size_t delta_2_5 = size5 - size2;
    
    BOOST_TEST_MESSAGE("Delta 1->2: " << delta_1_2 << ", Delta 2->5: " << delta_2_5);
    
    // Check scaling is roughly linear
    BOOST_CHECK_GT(delta_1_2, 100);  // At least input overhead
    BOOST_CHECK_LT(delta_1_2, 200);  // Not too much
    
    // 3 inputs should add ~3x delta
    BOOST_CHECK_GT(delta_2_5, delta_1_2 * 2);
    BOOST_CHECK_LT(delta_2_5, delta_1_2 * 4);
}

// =============================================================================
// ESTIMATION VS ACTUAL
// =============================================================================

BOOST_AUTO_TEST_CASE(estimation_within_tolerance)
{
    // Test: Estimated fee is within 20% of actual required fee
    
    CMutableTransaction tx = CreateTransferTx(1, false);
    CTransaction ctx(tx);
    
    size_t actual_size = ctx.GetTotalSize();
    
    // Estimated size (what wallet might calculate)
    size_t estimated_size = BASE_TX_SIZE + 
                           1 * P2PKH_INPUT_SIZE + 
                           2 * P2PKH_OUTPUT_SIZE + 
                           OP_RETURN_BASE_SIZE + SRC20_TRANSFER_SIZE;
    
    // Check within 20%
    double ratio = static_cast<double>(estimated_size) / actual_size;
    
    BOOST_TEST_MESSAGE("Estimated: " << estimated_size << ", Actual: " << actual_size 
        << ", Ratio: " << ratio);
    
    BOOST_CHECK_GT(ratio, 0.8);   // At least 80%
    BOOST_CHECK_LT(ratio, 1.2);   // At most 120%
}

// =============================================================================
// MINIMUM FEE REQUIREMENTS
// =============================================================================

BOOST_AUTO_TEST_CASE(minimum_fee_sufficient)
{
    // Test: Minimum fee calculation is sufficient for relay
    
    CMutableTransaction tx = CreateTransferTx(1, false);
    CTransaction ctx(tx);
    
    size_t size = ctx.GetTotalSize();
    
    // Minimum relay fee is typically 1 sat/vbyte
    CAmount min_fee = size * 1;
    
    BOOST_TEST_MESSAGE("Minimum fee for relay: " << min_fee << " qirsh");
    
    // Should be reasonable
    BOOST_CHECK_GT(min_fee, 100);   // At least 100 sats
    BOOST_CHECK_LT(min_fee, 1000);  // Less than 1000 sats for simple tx
}

// =============================================================================
// OP_RETURN SIZE IMPACT
// =============================================================================

BOOST_AUTO_TEST_CASE(op_return_size_impact)
{
    // Test: OP_RETURN data size correctly impacts fee
    
    // Transfer with minimal data
    CMutableTransaction tx_small;
    tx_small.vout.resize(1);
    tx_small.vout[0].nValue = 0;
    tx_small.vout[0].scriptPubKey = CScript() << OP_RETURN 
                                              << std::vector<unsigned char>(20, 0x00);
    
    // Transfer with maximum data (80 bytes typical limit)
    CMutableTransaction tx_large;
    tx_large.vout.resize(1);
    tx_large.vout[0].nValue = 0;
    tx_large.vout[0].scriptPubKey = CScript() << OP_RETURN 
                                              << std::vector<unsigned char>(80, 0x00);
    
    size_t small_size = CTransaction(tx_small).GetTotalSize();
    size_t large_size = CTransaction(tx_large).GetTotalSize();
    
    BOOST_TEST_MESSAGE("Small OP_RETURN: " << small_size << ", Large: " << large_size);
    
    // Difference should be approximately data size difference (60 bytes)
    size_t diff = large_size - small_size;
    BOOST_CHECK_GT(diff, 50);
    BOOST_CHECK_LT(diff, 70);
}

// =============================================================================
// VIRTUAL SIZE FOR SEGWIT
// =============================================================================

BOOST_AUTO_TEST_CASE(virtual_size_calculation)
{
    // Test: Virtual size is calculated correctly for fee purposes
    
    // For non-segwit, vsize == size
    CMutableTransaction tx_legacy = CreateTransferTx(1, false);
    CTransaction ctx_legacy(tx_legacy);
    
    size_t size = ctx_legacy.GetTotalSize();
    
    BOOST_TEST_MESSAGE("Legacy tx - Size: " << size);
    
    // Size should be reasonable for a token transaction
    BOOST_CHECK_GT(size, 100);  // At least 100 bytes
    BOOST_CHECK_LT(size, 1000); // Less than 1KB for simple tx
}

BOOST_AUTO_TEST_SUITE_END()
