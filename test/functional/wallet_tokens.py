#!/usr/bin/env python3
# Copyright (c) 2025 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test wallet-integrated SRC-20 token RPC commands.

This test verifies full token lifecycle using wallet-integrated RPCs:
1. walletissuetoken - Create and broadcast token issuance transaction
2. wallettransfertoken - Transfer tokens between addresses
3. walletburntoken - Burn (destroy) tokens
4. gettokenbalances - Get all token balances for wallet
5. gettokentxhistory - Get token transaction history

Note: These RPCs require a wallet and create actual transactions that are
broadcast to the network. They are different from the non-wallet RPCs
(issuetoken, transfertoken, burntoken) which only return OP_RETURN script data.
"""

from test_framework.test_framework import OpenSYTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
)
import time


class WalletTokenRPCTest(OpenSYTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.rpc_timeout = 600  # 10 minutes for RandomX mining
        self.extra_args = [
            ["-randomxforkheight=5"],
            ["-randomxforkheight=5"],
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        node2 = self.nodes[1]

        self.log.info("Mining initial blocks...")
        self.address = node.getnewaddress()
        self.generatetoaddress(node, 110, self.address)
        
        # Sync nodes
        self.sync_all()

        self.log.info("Testing gettokenbalances (empty)...")
        self.test_gettokenbalances_empty()

        self.log.info("Testing walletissuetoken...")
        token_id = self.test_walletissuetoken()

        self.log.info("Testing token confirmation...")
        self.test_token_confirmation(token_id)

        self.log.info("Testing gettokenbalances (with token)...")
        self.test_gettokenbalances_with_token(token_id)

        self.log.info("Testing wallettransfertoken...")
        self.test_wallettransfertoken(token_id)

        self.log.info("Testing walletburntoken...")
        self.test_walletburntoken(token_id)

        self.log.info("Testing gettokentxhistory...")
        self.test_gettokentxhistory(token_id)

        self.log.info("Testing error handling...")
        self.test_error_handling()

        self.log.info("All wallet token RPC tests passed!")

    def test_gettokenbalances_empty(self):
        """Test gettokenbalances when wallet has no tokens."""
        node = self.nodes[0]
        
        balances = node.gettokenbalances()
        assert isinstance(balances, list)
        assert_equal(len(balances), 0)

    def test_walletissuetoken(self):
        """Test creating a token with walletissuetoken."""
        node = self.nodes[0]
        
        # Issue a new token (ticker max 4 chars)
        result = node.walletissuetoken("WTST", "Wallet Test Token", 8, 100000000000)  # 1000 tokens with 8 decimals
        
        # Verify expected fields are present
        assert 'txid' in result
        assert 'token_id' in result
        assert 'ticker' in result
        assert 'name' in result
        assert 'decimals' in result
        assert 'total_supply' in result
        assert 'issuer_address' in result
        assert 'fee' in result
        
        # Verify values
        assert_equal(result['ticker'], "WTST")
        assert_equal(result['name'], "Wallet Test Token")
        assert_equal(result['decimals'], 8)
        assert_equal(result['total_supply'], 100000000000)
        
        # Verify fee is reasonable
        assert_greater_than(float(result['fee']), 0)
        
        self.log.info(f"Issued token: {result['token_id']}")
        self.log.info(f"Transaction: {result['txid']}")
        self.log.info(f"Fee: {result['fee']} SYL")
        
        # Store for later tests
        self.token_id = result['token_id']
        self.issue_txid = result['txid']
        
        return result['token_id']

    def test_token_confirmation(self, token_id):
        """Confirm the token by mining a block."""
        node = self.nodes[0]
        
        # Mine a block to confirm the token issuance
        self.generatetoaddress(node, 1, self.address)
        
        # Sync nodes
        self.sync_all()
        
        # Wait a moment for token processing
        time.sleep(0.5)
        
        # Verify token exists in database
        token_info = node.gettokeninfo(token_id)
        assert_equal(token_info['token_id'], token_id)
        assert_equal(token_info['ticker'], "WTST")
        assert_equal(token_info['name'], "Wallet Test Token")
        assert_equal(token_info['decimals'], 8)
        assert_equal(token_info['total_supply'], 100000000000)
        
        # Verify token count increased
        stats = node.gettokenstats()
        assert_greater_than(stats['token_count'], 0)
        
        self.log.info(f"Token confirmed with {token_info['holder_count']} holders")

    def test_gettokenbalances_with_token(self, token_id):
        """Test gettokenbalances after issuing a token."""
        node = self.nodes[0]
        
        balances = node.gettokenbalances()
        assert isinstance(balances, list)
        
        # We should have at least one token
        assert_greater_than(len(balances), 0)
        
        # Find our token
        our_token = None
        for balance in balances:
            if balance['token_id'] == token_id:
                our_token = balance
                break
        
        assert our_token is not None, f"Token {token_id} not found in balances"
        assert_equal(our_token['ticker'], "WTST")
        assert_equal(our_token['balance'], 100000000000)  # 1000 tokens
        
        self.log.info(f"Token balance: {our_token['balance_formatted']} WTST")

    def test_wallettransfertoken(self, token_id):
        """Test transferring tokens with wallettransfertoken."""
        node = self.nodes[0]
        node2 = self.nodes[1]
        
        # Get an address from node2
        recipient_address = node2.getnewaddress()
        
        # Transfer 100 tokens (10000000000 smallest units)
        transfer_amount = 10000000000
        result = node.wallettransfertoken(token_id, recipient_address, transfer_amount)
        
        # Verify expected fields
        assert 'txid' in result
        assert 'token_id' in result
        assert 'amount' in result
        assert 'to' in result
        assert 'fee' in result
        
        assert_equal(result['token_id'], token_id)
        assert_equal(result['amount'], transfer_amount)
        assert_equal(result['to'], recipient_address)
        
        self.log.info(f"Transfer txid: {result['txid']}")
        self.log.info(f"Fee: {result['fee']} SYL")
        
        # Mine a block to confirm the transfer
        self.generatetoaddress(node, 1, self.address)
        self.sync_all()
        time.sleep(0.5)
        
        # Verify sender balance decreased
        sender_balances = node.gettokenbalances()
        sender_token = None
        for balance in sender_balances:
            if balance['token_id'] == token_id:
                sender_token = balance
                break
        
        expected_sender_balance = 100000000000 - transfer_amount
        assert sender_token is not None
        assert_equal(sender_token['balance'], expected_sender_balance)
        
        # Verify recipient received tokens
        recipient_balance = node2.gettokenbalance(recipient_address, token_id)
        assert isinstance(recipient_balance, list)
        assert_greater_than(len(recipient_balance), 0)
        assert_equal(recipient_balance[0]['balance'], transfer_amount)
        
        self.log.info(f"Sender remaining balance: {sender_token['balance_formatted']}")
        self.log.info(f"Recipient balance: {recipient_balance[0]['balance_formatted']}")

    def test_walletburntoken(self, token_id):
        """Test burning tokens with walletburntoken."""
        node = self.nodes[0]
        
        # Get current balance
        balances = node.gettokenbalances()
        current_balance = None
        for balance in balances:
            if balance['token_id'] == token_id:
                current_balance = balance['balance']
                break
        
        assert current_balance is not None
        
        # Burn 50 tokens (5000000000 smallest units)
        burn_amount = 5000000000
        result = node.walletburntoken(token_id, burn_amount)
        
        # Verify expected fields
        assert 'txid' in result
        assert 'token_id' in result
        assert 'amount' in result
        assert 'fee' in result
        
        assert_equal(result['token_id'], token_id)
        assert_equal(result['amount'], burn_amount)
        
        self.log.info(f"Burn txid: {result['txid']}")
        self.log.info(f"Fee: {result['fee']} SYL")
        
        # Mine a block to confirm the burn
        self.generatetoaddress(node, 1, self.address)
        time.sleep(0.5)
        
        # Verify balance decreased
        balances = node.gettokenbalances()
        new_balance = None
        for balance in balances:
            if balance['token_id'] == token_id:
                new_balance = balance['balance']
                break
        
        expected_balance = current_balance - burn_amount
        assert_equal(new_balance, expected_balance)
        
        self.log.info(f"Balance after burn: {new_balance}")

    def test_gettokentxhistory(self, token_id):
        """Test getting token transaction history."""
        node = self.nodes[0]
        
        # Get full history
        history = node.gettokentxhistory()
        assert isinstance(history, list)
        
        # We should have at least 3 transactions (issue, transfer, burn)
        assert_greater_than(len(history), 2)
        
        self.log.info(f"Found {len(history)} token transactions in history")
        
        # Filter by token_id
        filtered_history = node.gettokentxhistory(token_id)
        assert isinstance(filtered_history, list)
        
        # All transactions should be for this token
        for tx in filtered_history:
            assert_equal(tx['token_id'], token_id)
            assert 'txid' in tx
            assert 'ticker' in tx
            assert 'amount' in tx
            
        self.log.info(f"Found {len(filtered_history)} transactions for token {token_id[:8]}...")

    def test_error_handling(self):
        """Test error handling for wallet token RPCs."""
        node = self.nodes[0]
        
        # walletissuetoken with duplicate ticker (token already exists)
        assert_raises_rpc_error(-8, "Ticker already exists", 
                               node.walletissuetoken, "WTST", "Duplicate", 8, 1000)
        
        # walletissuetoken with reserved ticker
        assert_raises_rpc_error(-8, None, 
                               node.walletissuetoken, "SYL", "Fake SYL", 8, 1000)
        
        # walletissuetoken with invalid parameters
        assert_raises_rpc_error(-8, None, 
                               node.walletissuetoken, "", "No Ticker", 8, 1000)
        assert_raises_rpc_error(-8, None, 
                               node.walletissuetoken, "LONG123", "Too Long", 8, 1000)
        assert_raises_rpc_error(-8, None, 
                               node.walletissuetoken, "DEC", "Bad Decimals", 25, 1000)
        
        # wallettransfertoken with non-existent token
        fake_token_id = "3" * 64
        dest_address = node.getnewaddress()
        assert_raises_rpc_error(-5, "Token not found", 
                               node.wallettransfertoken, fake_token_id, dest_address, 1000)
        
        # wallettransfertoken with invalid amount
        assert_raises_rpc_error(-8, "Amount must be greater than zero",
                               node.wallettransfertoken, self.token_id, dest_address, 0)
        
        # walletburntoken with non-existent token
        assert_raises_rpc_error(-5, "Token not found", 
                               node.walletburntoken, fake_token_id, 1000)
        
        # walletburntoken with invalid amount
        assert_raises_rpc_error(-8, "Amount must be greater than zero",
                               node.walletburntoken, self.token_id, 0)
        
        self.log.info("Error handling tests passed")


if __name__ == '__main__':
    WalletTokenRPCTest(__file__).main()
