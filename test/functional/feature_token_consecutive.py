#!/usr/bin/env python3
# Copyright (c) 2025 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test for consecutive unconfirmed token transfers.

This test specifically targets the bug where the node crashes during
consecutive wallettransfertoken calls when the first TX is still in mempool.

The issue manifests as:
- First transfer succeeds, enters mempool
- Second transfer causes node crash (SIGSEGV/SIGABRT)
- Root cause: NULL pointer dereference in AvailableCoins() when coinControl is nullptr
  but params.check_version_trucness is true

This test verifies the fix for this issue.
"""

from test_framework.test_framework import OpenSYTestFramework
from test_framework.util import assert_equal


class TokenConsecutiveTransferTest(OpenSYTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [["-randomxforkheight=5"], ["-randomxforkheight=5"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        sender = self.nodes[0]
        receiver = self.nodes[1]
        
        # Setup - generate many blocks to have plenty of UTXOs
        self.log.info("Setting up test environment...")
        addr = sender.getnewaddress()
        self.generatetoaddress(sender, 200, addr)  # Generate extra blocks
        self.sync_blocks()
        
        # Create a token
        self.log.info("Creating test token...")
        result = sender.walletissuetoken("CONS", "Consecutive Test", 8, 1000000)
        token_id = result['token_id']
        self.generatetoaddress(sender, 1, addr)
        self.sync_blocks()
        
        # Get recipient address from different node
        recipient = receiver.getnewaddress()
        
        # Test 1: Two consecutive transfers (this was the crash case)
        self.log.info("Test 1: Two consecutive transfers (crash test)...")
        tx1 = sender.wallettransfertoken(token_id, recipient, 10000)
        self.log.info("  First transfer: %s", tx1['txid'])
        sender.syncwithvalidationinterfacequeue()
        tx2 = sender.wallettransfertoken(token_id, recipient, 20000)
        self.log.info("  Second transfer: %s", tx2['txid'])
        self.generatetoaddress(sender, 1, addr)
        self.sync_blocks()
        
        # Check sender balance decreased
        sender_bal = sender.gettokenbalances()
        bal = next((b['balance'] for b in sender_bal if b['token_id'] == token_id), 0)
        assert_equal(bal, 1000000 - 30000)
        self.log.info("  PASSED - Sender balance: %d", bal)
        
        # Check receiver got the tokens
        recv_bal = receiver.gettokenbalances()
        rbal = next((b['balance'] for b in recv_bal if b['token_id'] == token_id), 0)
        assert_equal(rbal, 30000)
        self.log.info("  PASSED - Receiver balance: %d", rbal)
        
        # Test 2: Three consecutive transfers without sync
        self.log.info("Test 2: Three consecutive transfers without sync...")
        sender.wallettransfertoken(token_id, recipient, 1000)
        sender.wallettransfertoken(token_id, recipient, 2000)
        sender.wallettransfertoken(token_id, recipient, 3000)
        self.generatetoaddress(sender, 1, addr)
        self.sync_blocks()
        
        sender_bal = sender.gettokenbalances()
        bal = next((b['balance'] for b in sender_bal if b['token_id'] == token_id), 0)
        assert_equal(bal, 1000000 - 30000 - 6000)
        self.log.info("  PASSED - Sender balance: %d", bal)
        
        # Test 3: Even more consecutive transfers
        self.log.info("Test 3: Five consecutive transfers...")
        sender.wallettransfertoken(token_id, recipient, 100)
        sender.wallettransfertoken(token_id, recipient, 100)
        sender.wallettransfertoken(token_id, recipient, 100)
        sender.wallettransfertoken(token_id, recipient, 100)
        sender.wallettransfertoken(token_id, recipient, 100)
        self.generatetoaddress(sender, 1, addr)
        self.sync_blocks()
        
        sender_bal = sender.gettokenbalances()
        bal = next((b['balance'] for b in sender_bal if b['token_id'] == token_id), 0)
        assert_equal(bal, 1000000 - 30000 - 6000 - 500)
        self.log.info("  PASSED - Sender balance: %d", bal)
        
        self.log.info("All consecutive transfer tests passed!")


if __name__ == '__main__':
    TokenConsecutiveTransferTest(__file__).main()
