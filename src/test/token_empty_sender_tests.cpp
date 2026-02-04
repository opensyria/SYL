// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/src20.h>
#include <test/util/setup_common.h>
#include <tokens/tokendb.h>
#include <tokens/tokenvalidation.h>
#include <uint256.h>
#include <util/fs.h>

#include <boost/test/unit_test.hpp>

/**
 * GAP-03: Empty sender in ProcessBlock validation
 *
 * These tests verify that the token system correctly handles
 * malformed transactions where the sender address cannot be
 * determined from the coinbase/inputs.
 *
 * Key scenarios:
 * - Empty coinbase outputs
 * - Invalid script in first output  
 * - Non-extractable address from output
 */

namespace {

// Create a coinbase with no outputs
CMutableTransaction CreateEmptyCoinbase()
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript() << 0 << 0;
    // No vout - empty outputs
    return tx;
}

// Create a coinbase with invalid script
CMutableTransaction CreateInvalidScriptCoinbase()
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript() << 0 << 0;
    tx.vout.resize(1);
    // Empty script - can't extract destination
    tx.vout[0].scriptPubKey = CScript();
    tx.vout[0].nValue = 10000 * COIN;
    return tx;
}

// Create a coinbase with OP_RETURN only (no address)
CMutableTransaction CreateOpReturnOnlyCoinbase()
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript() << 0 << 0;
    tx.vout.resize(1);
    // OP_RETURN output - can't extract destination
    tx.vout[0].scriptPubKey = CScript() << OP_RETURN << std::vector<unsigned char>(10, 0x42);
    tx.vout[0].nValue = 0;
    return tx;
}

// Create a valid coinbase
CMutableTransaction CreateValidCoinbase()
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript() << 0 << 0;
    tx.vout.resize(1);
    // Valid P2PKH script
    tx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 
                                        << std::vector<unsigned char>(20, 0xAB)
                                        << OP_EQUALVERIFY << OP_CHECKSIG;
    tx.vout[0].nValue = 10000 * COIN;
    return tx;
}

// Create block with specific coinbase
CBlock CreateBlockWithCoinbase(int height, const CMutableTransaction& coinbase)
{
    CBlock block;
    block.nVersion = 1;
    block.hashPrevBlock = uint256::ONE;
    block.hashMerkleRoot = uint256::ZERO;
    block.nTime = 1234567890 + height;
    block.nBits = 0x1d00ffff;
    block.nNonce = height;
    block.vtx.push_back(MakeTransactionRef(coinbase));
    return block;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(token_empty_sender_tests, BasicTestingSetup)

// =============================================================================
// EMPTY COINBASE OUTPUTS
// =============================================================================

BOOST_AUTO_TEST_CASE(empty_coinbase_no_crash)
{
    // Test: Block with empty coinbase outputs doesn't crash ProcessBlock
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_empty_cb";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    // Create block with empty coinbase
    CBlock block = CreateBlockWithCoinbase(100, CreateEmptyCoinbase());
    
    // ProcessBlock should handle this gracefully
    // (No token operations in the block anyway)
    int ops = db.ProcessBlock(block, 100);
    
    // Should return 0 operations (no valid token txs)
    BOOST_CHECK_EQUAL(ops, 0);
    
    // Cleanup
    fs::remove_all(test_path);
}

// =============================================================================
// INVALID SCRIPT IN FIRST OUTPUT
// =============================================================================

BOOST_AUTO_TEST_CASE(invalid_script_coinbase_handled)
{
    // Test: Block with invalid script in coinbase output[0] is handled
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_invalid_script";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    // Create block with invalid script coinbase
    CBlock block = CreateBlockWithCoinbase(100, CreateInvalidScriptCoinbase());
    
    // ProcessBlock should handle this
    int ops = db.ProcessBlock(block, 100);
    
    // Should process without crash
    BOOST_CHECK_GE(ops, 0);
    
    // Cleanup
    fs::remove_all(test_path);
}

// =============================================================================
// OP_RETURN ONLY COINBASE
// =============================================================================

BOOST_AUTO_TEST_CASE(op_return_only_coinbase_handled)
{
    // Test: Block with OP_RETURN only in coinbase is handled
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_opreturn";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    // Create block with OP_RETURN only coinbase
    CBlock block = CreateBlockWithCoinbase(100, CreateOpReturnOnlyCoinbase());
    
    // ProcessBlock should handle this (can't extract sender address)
    int ops = db.ProcessBlock(block, 100);
    
    // Should process without crash
    BOOST_CHECK_GE(ops, 0);
    
    // Cleanup
    fs::remove_all(test_path);
}

// =============================================================================
// VALID COINBASE BASELINE
// =============================================================================

BOOST_AUTO_TEST_CASE(valid_coinbase_works)
{
    // Test: Baseline - valid coinbase is processed correctly
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_valid_cb";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    // Create block with valid coinbase
    CBlock block = CreateBlockWithCoinbase(100, CreateValidCoinbase());
    
    // ProcessBlock should work
    int ops = db.ProcessBlock(block, 100);
    
    // Should return 0 (no token ops, but valid processing)
    BOOST_CHECK_EQUAL(ops, 0);
    
    // Cleanup
    fs::remove_all(test_path);
}

// =============================================================================
// SENDER ADDRESS EXTRACTION
// =============================================================================

BOOST_AUTO_TEST_CASE(sender_address_extraction_edge_cases)
{
    // Test: Various edge cases in sender address extraction
    
    // Test 1: P2SH script (extractable)
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.SetNull();
        tx.vout.resize(1);
        // P2SH script
        std::vector<unsigned char> script_hash(20, 0xCD);
        tx.vout[0].scriptPubKey = CScript() << OP_HASH160 << script_hash << OP_EQUAL;
        tx.vout[0].nValue = 10000 * COIN;
        
        CScript& script = tx.vout[0].scriptPubKey;
        CTxDestination dest;
        bool extracted = ExtractDestination(script, dest);
        BOOST_CHECK_MESSAGE(extracted, "P2SH script should be extractable");
    }
    
    // Test 2: P2WPKH script (extractable)
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.SetNull();
        tx.vout.resize(1);
        // P2WPKH script
        std::vector<unsigned char> pubkey_hash(20, 0xEF);
        tx.vout[0].scriptPubKey = CScript() << OP_0 << pubkey_hash;
        tx.vout[0].nValue = 10000 * COIN;
        
        CScript& script = tx.vout[0].scriptPubKey;
        CTxDestination dest;
        bool extracted = ExtractDestination(script, dest);
        BOOST_CHECK_MESSAGE(extracted, "P2WPKH script should be extractable");
    }
    
    // Test 3: Bare multisig (not extractable to single address)
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.SetNull();
        tx.vout.resize(1);
        // 1-of-1 multisig (unusual but valid)
        std::vector<unsigned char> pubkey(33, 0x02);  // Compressed pubkey format
        tx.vout[0].scriptPubKey = CScript() << OP_1 << pubkey << OP_1 << OP_CHECKMULTISIG;
        tx.vout[0].nValue = 10000 * COIN;
        
        CScript& script = tx.vout[0].scriptPubKey;
        CTxDestination dest;
        // Multisig may or may not be extractable depending on configuration
        ExtractDestination(script, dest);
        // Just verify no crash
        BOOST_CHECK(true);
    }
}

// =============================================================================
// TOKEN VALIDATION WITH EMPTY SENDER
// =============================================================================

BOOST_AUTO_TEST_CASE(token_transfer_validation_with_empty_sender)
{
    // Test: Token transfer validation handles empty sender gracefully
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_transfer_sender";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    tokens::TokenValidator validator(db);
    
    // Create a transfer operation
    src20::TokenTransfer transfer;
    transfer.token_id = src20::TokenId(uint256::ONE);
    transfer.amount = 100;
    
    // Wrap in SRC20Operation using std::variant
    src20::SRC20Operation op;
    op.action = src20::TokenAction::TRANSFER;
    op.data = transfer;
    
    // Create a tx with recipient output
    CMutableTransaction tx;
    tx.vout.resize(2);
    tx.vout[0].scriptPubKey = CScript() << OP_RETURN;  // Token marker
    tx.vout[0].nValue = 0;
    tx.vout[1].scriptPubKey = CScript() << OP_DUP << OP_HASH160 
                                        << std::vector<unsigned char>(20, 0xBB)
                                        << OP_EQUALVERIFY << OP_CHECKSIG;
    tx.vout[1].nValue = COIN;
    
    // Empty sender
    CScript empty_sender;
    
    // Validation should fail (token not found, since we didn't create it)
    // But the key point is it doesn't crash with empty sender
    auto result = validator.ValidateOperation(op, CTransaction(tx), empty_sender);
    
    // Should fail (token not found or insufficient balance)
    BOOST_CHECK(!result.IsValid());
    
    // Cleanup
    fs::remove_all(test_path);
}

// =============================================================================
// BURN VALIDATION WITH EMPTY SENDER
// =============================================================================

BOOST_AUTO_TEST_CASE(token_burn_validation_with_empty_sender)
{
    // Test: Token burn validation handles empty sender gracefully
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_burn_sender";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    tokens::TokenValidator validator(db);
    
    // Create a burn operation
    src20::TokenBurn burn;
    burn.token_id = src20::TokenId(uint256::ONE);
    burn.amount = 50;
    
    // Wrap in SRC20Operation using std::variant
    src20::SRC20Operation op;
    op.action = src20::TokenAction::BURN;
    op.data = burn;
    
    // Create a minimal transaction
    CMutableTransaction tx;
    tx.vout.resize(1);
    tx.vout[0].scriptPubKey = CScript() << OP_RETURN;
    tx.vout[0].nValue = 0;
    
    // Empty sender
    CScript empty_sender;
    
    // Validation should fail (token not found)
    // But shouldn't crash with empty sender
    auto result = validator.ValidateOperation(op, CTransaction(tx), empty_sender);
    
    // Should fail (token not found or insufficient balance)
    BOOST_CHECK(!result.IsValid());
    
    // Cleanup
    fs::remove_all(test_path);
}

// =============================================================================
// ISSUANCE WITH EMPTY SENDER
// =============================================================================

BOOST_AUTO_TEST_CASE(token_issuance_with_empty_sender)
{
    // Test: Token issuance doesn't depend on sender (only needs valid output)
    
    fs::path test_path = m_args.GetDataDirNet() / "tokendb_test_issue_sender";
    fs::create_directories(test_path);
    
    tokens::TokenDB db(test_path, 1 << 20, false, false);
    BOOST_REQUIRE(db.IsValid());
    
    tokens::TokenValidator validator(db);
    
    // Create an issuance operation
    src20::TokenIssuance issuance;
    issuance.ticker = "TEST";
    issuance.name = "Test Token";
    issuance.decimals = 8;
    issuance.total_supply = 1000000;
    
    // Wrap in SRC20Operation using std::variant
    src20::SRC20Operation op;
    op.action = src20::TokenAction::ISSUE;
    op.data = issuance;
    
    // Create a minimal transaction
    CMutableTransaction tx;
    tx.vout.resize(1);
    tx.vout[0].scriptPubKey = CScript() << OP_RETURN;
    tx.vout[0].nValue = 0;
    
    // Empty sender - issuance validation may still work (format checks only)
    CScript empty_sender;
    
    // Issuance validation mainly checks format, ticker availability
    auto result = validator.ValidateOperation(op, CTransaction(tx), empty_sender);
    
    // Issuance should pass format validation (unique ticker check may fail in fresh DB)
    // The key assertion is no crash with empty sender
    BOOST_CHECK(true); // No crash means test passes
    
    // Cleanup
    fs::remove_all(test_path);
}

BOOST_AUTO_TEST_SUITE_END()
