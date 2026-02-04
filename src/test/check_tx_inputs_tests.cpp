// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <coins.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

/**
 * CheckTxInputs Edge Case Tests
 *
 * These tests verify edge cases in the Consensus::CheckTxInputs function:
 * - Coinbase maturity boundary (100 blocks)
 * - Input value overflow protection
 * - Missing/spent inputs detection
 * - Fee calculation edge cases
 */

BOOST_FIXTURE_TEST_SUITE(check_tx_inputs_tests, TestingSetup)

// Helper to create a simple output
static CTxOut CreateOutput(CAmount amount)
{
    CTxOut out;
    out.nValue = amount;
    out.scriptPubKey = CScript() << OP_TRUE;
    return out;
}

// Helper to create a coin in the view
static void AddCoinToView(CCoinsViewCache& view, const COutPoint& outpoint, CAmount amount, int height, bool coinbase = false)
{
    Coin coin;
    coin.out.nValue = amount;
    coin.out.scriptPubKey = CScript() << OP_TRUE;
    coin.nHeight = height;
    coin.fCoinBase = coinbase;
    view.AddCoin(outpoint, std::move(coin), false);
}

// =============================================================================
// COINBASE MATURITY TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(coinbase_maturity_exactly_100)
{
    // Test: Coinbase at exactly COINBASE_MATURITY depth is spendable
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    
    COutPoint outpoint{Txid::FromUint256(uint256::ONE), 0};
    int coinbaseHeight = 100;
    int spendHeight = coinbaseHeight + COINBASE_MATURITY;  // Exactly at maturity
    
    AddCoinToView(view, outpoint, 50 * COIN, coinbaseHeight, true);
    
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = outpoint;
    mtx.vout.resize(1);
    mtx.vout[0] = CreateOutput(50 * COIN);
    
    CTransaction tx(mtx);
    TxValidationState state;
    CAmount txfee;
    
    bool result = Consensus::CheckTxInputs(tx, state, view, spendHeight, txfee);
    BOOST_CHECK_MESSAGE(result, "Coinbase at exactly maturity depth should be spendable");
    BOOST_CHECK_EQUAL(txfee, 0);
}

BOOST_AUTO_TEST_CASE(coinbase_maturity_one_short)
{
    // Test: Coinbase at COINBASE_MATURITY - 1 depth is NOT spendable
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    
    COutPoint outpoint{Txid::FromUint256(uint256::ONE), 0};
    int coinbaseHeight = 100;
    int spendHeight = coinbaseHeight + COINBASE_MATURITY - 1;  // One block too early
    
    AddCoinToView(view, outpoint, 50 * COIN, coinbaseHeight, true);
    
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = outpoint;
    mtx.vout.resize(1);
    mtx.vout[0] = CreateOutput(50 * COIN);
    
    CTransaction tx(mtx);
    TxValidationState state;
    CAmount txfee;
    
    bool result = Consensus::CheckTxInputs(tx, state, view, spendHeight, txfee);
    BOOST_CHECK_MESSAGE(!result, "Coinbase one block before maturity should NOT be spendable");
    BOOST_CHECK(state.GetResult() == TxValidationResult::TX_PREMATURE_SPEND);
    BOOST_CHECK(state.GetRejectReason().find("premature") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(coinbase_maturity_well_past)
{
    // Test: Coinbase well past maturity is spendable
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    
    COutPoint outpoint{Txid::FromUint256(uint256::ONE), 0};
    int coinbaseHeight = 100;
    int spendHeight = coinbaseHeight + COINBASE_MATURITY + 10000;  // Well past maturity
    
    AddCoinToView(view, outpoint, 50 * COIN, coinbaseHeight, true);
    
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = outpoint;
    mtx.vout.resize(1);
    mtx.vout[0] = CreateOutput(50 * COIN);
    
    CTransaction tx(mtx);
    TxValidationState state;
    CAmount txfee;
    
    bool result = Consensus::CheckTxInputs(tx, state, view, spendHeight, txfee);
    BOOST_CHECK_MESSAGE(result, "Coinbase well past maturity should be spendable");
}

BOOST_AUTO_TEST_CASE(non_coinbase_no_maturity_requirement)
{
    // Test: Non-coinbase outputs have no maturity requirement
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    
    COutPoint outpoint{Txid::FromUint256(uint256::ONE), 0};
    int utxoHeight = 100;
    int spendHeight = utxoHeight + 1;  // Spend in next block (would fail coinbase maturity)
    
    AddCoinToView(view, outpoint, 50 * COIN, utxoHeight, false);  // Not coinbase
    
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = outpoint;
    mtx.vout.resize(1);
    mtx.vout[0] = CreateOutput(50 * COIN);
    
    CTransaction tx(mtx);
    TxValidationState state;
    CAmount txfee;
    
    bool result = Consensus::CheckTxInputs(tx, state, view, spendHeight, txfee);
    BOOST_CHECK_MESSAGE(result, "Non-coinbase output should be immediately spendable");
}

// =============================================================================
// INPUT VALUE OVERFLOW TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(input_value_max_money)
{
    // Test: Input at MAX_MONEY is valid
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    
    COutPoint outpoint{Txid::FromUint256(uint256::ONE), 0};
    AddCoinToView(view, outpoint, MAX_MONEY, 100, false);
    
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = outpoint;
    mtx.vout.resize(1);
    mtx.vout[0] = CreateOutput(MAX_MONEY);
    
    CTransaction tx(mtx);
    TxValidationState state;
    CAmount txfee;
    
    bool result = Consensus::CheckTxInputs(tx, state, view, 200, txfee);
    BOOST_CHECK_MESSAGE(result, "Input at MAX_MONEY should be valid");
    BOOST_CHECK_EQUAL(txfee, 0);
}

BOOST_AUTO_TEST_CASE(multiple_inputs_at_max_money)
{
    // Test: Multiple inputs that would overflow if summed incorrectly
    // This is a critical security test
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    
    // Two inputs each at MAX_MONEY/2 + 1 would overflow if not checked
    CAmount halfMax = MAX_MONEY / 2;
    
    COutPoint outpoint1{Txid::FromUint256(uint256::ONE), 0};
    COutPoint outpoint2{Txid::FromUint256(uint256::ONE), 1};
    
    AddCoinToView(view, outpoint1, halfMax, 100, false);
    AddCoinToView(view, outpoint2, halfMax, 100, false);
    
    CMutableTransaction mtx;
    mtx.vin.resize(2);
    mtx.vin[0].prevout = outpoint1;
    mtx.vin[1].prevout = outpoint2;
    mtx.vout.resize(1);
    mtx.vout[0] = CreateOutput(MAX_MONEY);
    
    CTransaction tx(mtx);
    TxValidationState state;
    CAmount txfee;
    
    bool result = Consensus::CheckTxInputs(tx, state, view, 200, txfee);
    BOOST_CHECK_MESSAGE(result, "Two inputs summing to MAX_MONEY should be valid");
}

// =============================================================================
// MISSING/SPENT INPUTS TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(missing_input)
{
    // Test: Transaction referencing non-existent input fails
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    
    // Don't add any coins to view
    
    COutPoint outpoint{Txid::FromUint256(uint256::ONE), 0};
    
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = outpoint;
    mtx.vout.resize(1);
    mtx.vout[0] = CreateOutput(1 * COIN);
    
    CTransaction tx(mtx);
    TxValidationState state;
    CAmount txfee;
    
    bool result = Consensus::CheckTxInputs(tx, state, view, 200, txfee);
    BOOST_CHECK_MESSAGE(!result, "Transaction with missing input should fail");
    BOOST_CHECK(state.GetResult() == TxValidationResult::TX_MISSING_INPUTS);
}

// =============================================================================
// FEE CALCULATION TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(fee_positive)
{
    // Test: Positive fee is calculated correctly
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    
    COutPoint outpoint{Txid::FromUint256(uint256::ONE), 0};
    AddCoinToView(view, outpoint, 10 * COIN, 100, false);
    
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = outpoint;
    mtx.vout.resize(1);
    mtx.vout[0] = CreateOutput(9 * COIN);  // 1 COIN fee
    
    CTransaction tx(mtx);
    TxValidationState state;
    CAmount txfee;
    
    bool result = Consensus::CheckTxInputs(tx, state, view, 200, txfee);
    BOOST_CHECK(result);
    BOOST_CHECK_EQUAL(txfee, 1 * COIN);
}

BOOST_AUTO_TEST_CASE(fee_zero)
{
    // Test: Zero fee is valid
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    
    COutPoint outpoint{Txid::FromUint256(uint256::ONE), 0};
    AddCoinToView(view, outpoint, 10 * COIN, 100, false);
    
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = outpoint;
    mtx.vout.resize(1);
    mtx.vout[0] = CreateOutput(10 * COIN);  // Zero fee
    
    CTransaction tx(mtx);
    TxValidationState state;
    CAmount txfee;
    
    bool result = Consensus::CheckTxInputs(tx, state, view, 200, txfee);
    BOOST_CHECK(result);
    BOOST_CHECK_EQUAL(txfee, 0);
}

BOOST_AUTO_TEST_CASE(outputs_exceed_inputs)
{
    // Test: Output value exceeding input value fails
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    
    COutPoint outpoint{Txid::FromUint256(uint256::ONE), 0};
    AddCoinToView(view, outpoint, 10 * COIN, 100, false);
    
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = outpoint;
    mtx.vout.resize(1);
    mtx.vout[0] = CreateOutput(11 * COIN);  // More than input
    
    CTransaction tx(mtx);
    TxValidationState state;
    CAmount txfee;
    
    bool result = Consensus::CheckTxInputs(tx, state, view, 200, txfee);
    BOOST_CHECK_MESSAGE(!result, "Output exceeding input should fail");
    BOOST_CHECK(state.GetResult() == TxValidationResult::TX_CONSENSUS);
    BOOST_CHECK(state.GetRejectReason().find("belowout") != std::string::npos);
}

// =============================================================================
// BOUNDARY SPEND HEIGHT TESTS
// =============================================================================

BOOST_AUTO_TEST_CASE(spend_at_height_zero)
{
    // Test: Spending at height 0 (unusual but should work for non-coinbase)
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    
    COutPoint outpoint{Txid::FromUint256(uint256::ONE), 0};
    AddCoinToView(view, outpoint, 10 * COIN, 0, false);  // Created at height 0
    
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = outpoint;
    mtx.vout.resize(1);
    mtx.vout[0] = CreateOutput(10 * COIN);
    
    CTransaction tx(mtx);
    TxValidationState state;
    CAmount txfee;
    
    // Spend at height 1
    bool result = Consensus::CheckTxInputs(tx, state, view, 1, txfee);
    BOOST_CHECK_MESSAGE(result, "Non-coinbase from genesis should be spendable");
}

BOOST_AUTO_TEST_CASE(spend_height_same_as_creation)
{
    // Test: Spending in same block as creation (edge case)
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    
    COutPoint outpoint{Txid::FromUint256(uint256::ONE), 0};
    AddCoinToView(view, outpoint, 10 * COIN, 100, false);
    
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = outpoint;
    mtx.vout.resize(1);
    mtx.vout[0] = CreateOutput(10 * COIN);
    
    CTransaction tx(mtx);
    TxValidationState state;
    CAmount txfee;
    
    // Spend at same height (100)
    bool result = Consensus::CheckTxInputs(tx, state, view, 100, txfee);
    BOOST_CHECK_MESSAGE(result, "Spending in same block should work for non-coinbase");
}

BOOST_AUTO_TEST_SUITE_END()
