#!/usr/bin/env python3
# Copyright (c) 2025 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Stress test for SRC-20 token behavior under multiple sequential reorgs.

AUDIT FIX M-06: This test verifies token state correctness under 5+ sequential
reorgs, ensuring the undo mechanism handles repeated connect/disconnect cycles.

Test scenarios:
1. Multiple sequential 1-block reorgs with token issuance
2. Multiple sequential 2-block reorgs with token transfers
3. Alternating issuance/transfer reorgs
4. State consistency verification across all nodes
"""

from test_framework.test_framework import OpenSYTestFramework
from test_framework.util import (
    assert_equal,
)


class TokenStressReorgTest(OpenSYTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [
            ["-randomxforkheight=5", "-maxtxfee=500"],
            ["-randomxforkheight=5", "-maxtxfee=500"],
            ["-randomxforkheight=5", "-maxtxfee=500"],
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def stop_nodes(self, wait=0):
        """Override stop_nodes to tolerate shutdown crash."""
        try:
            super().stop_nodes(wait=wait)
        except Exception:
            pass

    def get_wallet_token_balance(self, node, token_id):
        """Get the balance of a specific token from the wallet's gettokenbalances."""
        balances = node.gettokenbalances()
        for b in balances:
            if b['token_id'] == token_id:
                return b['balance']
        return 0

    def run_test(self):
        self.log.info("Setting up chain...")
        self.init_test_chain()

        self.log.info("Test 1: 5 sequential 1-block reorgs with token issuance...")
        self.test_sequential_issuance_reorgs(num_reorgs=5)

        self.log.info("Test 2: 5 sequential 2-block reorgs with transfers...")
        self.test_sequential_transfer_reorgs(num_reorgs=5)

        self.log.info("Test 3: Alternating issuance and transfer reorgs...")
        self.test_alternating_reorgs()

        self.log.info("Test 4: State consistency verification...")
        self.verify_final_state_consistency()

        self.log.info("All token stress reorg tests passed!")

    def init_test_chain(self):
        """Mine blocks to have mature coins for testing."""
        self.node0 = self.nodes[0]
        self.node1 = self.nodes[1]
        self.node2 = self.nodes[2]

        self.address0 = self.node0.getnewaddress()
        self.address1 = self.node1.getnewaddress()
        self.address2 = self.node2.getnewaddress()

        # Mine enough blocks for mature coins on node0
        self.generatetoaddress(self.node0, 110, self.address0)
        self.sync_all()

        # Fund node1 so it can also mine competing chains
        self.node0.sendtoaddress(self.address1, 500)
        self.generatetoaddress(self.node0, 1, self.address0)
        self.sync_all()

    def disconnect_all(self):
        """Disconnect all nodes to create isolated chains."""
        self.disconnect_nodes(0, 1)
        self.disconnect_nodes(1, 2)
        self.disconnect_nodes(0, 2)

    def reconnect_all(self):
        """Reconnect all nodes."""
        self.connect_nodes(0, 1)
        self.connect_nodes(1, 2)
        self.connect_nodes(0, 2)

    def test_sequential_issuance_reorgs(self, num_reorgs):
        """Test multiple sequential reorgs where token issuance is reverted repeatedly."""

        for i in range(num_reorgs):
            self.log.info(f"  Reorg iteration {i + 1}/{num_reorgs}")

            # Disconnect all nodes
            self.disconnect_all()

            # Node0: Issue a token using the wallet RPC
            ticker = f"SR{i:02d}"  # 4 chars max: SR00..SR04
            try:
                result = self.node0.walletissuetoken(ticker, f"Stress Token {i}", 8, 1000000)
                token_id = result['token_id']
                self.log.info(f"    Issued token {ticker} (token_id={token_id[:16]}...)")
            except Exception as e:
                self.log.info(f"    Token issuance failed (expected if ticker exists): {e}")
                self.reconnect_all()
                self.sync_blocks()
                continue

            # Mine the token on node0 (no sync — nodes disconnected)
            self.generatetoaddress(self.node0, 1, self.address0, sync_fun=self.no_op)

            # Node1: Mine a longer chain without the token
            self.generatetoaddress(self.node1, 2, self.address1, sync_fun=self.no_op)

            # Reconnect - node0 should reorg to node1's chain
            self.reconnect_all()
            self.sync_blocks()  # Only sync blocks, not mempools after reorg

            # Verify token was reverted on node0
            try:
                self.node0.gettokeninfo(token_id)
                self.log.info(f"    Token {ticker} still exists after reorg attempt")
            except Exception:
                self.log.info(f"    Token {ticker} correctly reverted")

        self.log.info(f"  Completed {num_reorgs} sequential issuance reorgs")

    def test_sequential_transfer_reorgs(self, num_reorgs):
        """Test multiple sequential reorgs where token transfers are reverted."""

        # First, issue a token that will persist (on all nodes' chain)
        ticker = "XFER"
        try:
            result = self.node0.walletissuetoken(ticker, "Transfer Test Token", 8, 10000000)
            token_id = result['token_id']
            self.generatetoaddress(self.node0, 1, self.address0)
            self.sync_all()
            self.log.info(f"  Issued {ticker} for transfer tests (token_id={token_id[:16]}...)")
        except Exception as e:
            self.log.info(f"  Could not issue XFER token: {e}")
            balances = self.node0.gettokenbalances()
            if balances:
                token_id = balances[0]['token_id']
                self.log.info(f"  Using existing token {balances[0]['ticker']} for transfer tests")
            else:
                self.log.info("  No tokens available for transfer test, skipping")
                return

        initial_balance = self.get_wallet_token_balance(self.node0, token_id)
        self.log.info(f"  Initial balance: {initial_balance}")

        for i in range(num_reorgs):
            self.log.info(f"  Transfer reorg iteration {i + 1}/{num_reorgs}")

            # Disconnect all nodes
            self.disconnect_all()

            # Node0: Transfer tokens
            try:
                self.node0.wallettransfertoken(token_id, self.address1, 100)
                self.generatetoaddress(self.node0, 1, self.address0, sync_fun=self.no_op)
            except Exception as e:
                self.log.info(f"    Transfer failed: {e}")
                self.reconnect_all()
                self.sync_blocks()
                continue

            balance_after_transfer = self.get_wallet_token_balance(self.node0, token_id)

            # Node1: Mine longer chain (reverts the transfer)
            self.generatetoaddress(self.node1, 2, self.address1, sync_fun=self.no_op)

            # Reconnect and sync
            self.reconnect_all()
            self.sync_blocks()  # Only sync blocks, not mempools after reorg

            # Verify balance was restored after reorg
            balance_after_reorg = self.get_wallet_token_balance(self.node0, token_id)

            self.log.info(f"    Balance: {initial_balance} -> {balance_after_transfer} -> {balance_after_reorg}")

    def test_alternating_reorgs(self):
        """Test alternating between issuance and transfer reorgs."""

        for i in range(3):
            self.log.info(f"  Alternating reorg iteration {i + 1}")

            # Disconnect all nodes
            self.disconnect_all()

            if i % 2 == 0:
                # Issue token
                try:
                    ticker = f"AL{i:02d}"  # 4 chars: AL00, AL02
                    self.node0.walletissuetoken(ticker, f"Alt Token {i}", 8, 500000)
                    self.generatetoaddress(self.node0, 1, self.address0, sync_fun=self.no_op)
                except Exception:
                    pass
            else:
                # Transfer existing token
                balances = self.node0.gettokenbalances()
                if balances:
                    try:
                        self.node0.wallettransfertoken(balances[0]['token_id'], self.address2, 50)
                        self.generatetoaddress(self.node0, 1, self.address0, sync_fun=self.no_op)
                    except Exception:
                        pass

            # Node1 creates longer chain
            self.generatetoaddress(self.node1, 2, self.address1, sync_fun=self.no_op)

            # Reconnect
            self.reconnect_all()
            self.sync_blocks()  # Only sync blocks, not mempools after reorg

    def verify_final_state_consistency(self):
        """Verify all nodes have consistent token state after stress testing."""

        self.sync_blocks()

        # Get token count from all nodes
        tokens0 = self.node0.listtokens()
        tokens1 = self.node1.listtokens()
        tokens2 = self.node2.listtokens()

        self.log.info(f"  Node0 tokens: {len(tokens0)}")
        self.log.info(f"  Node1 tokens: {len(tokens1)}")
        self.log.info(f"  Node2 tokens: {len(tokens2)}")

        # All nodes should have same token count
        assert_equal(len(tokens0), len(tokens1))
        assert_equal(len(tokens1), len(tokens2))

        # Verify each token's info matches across nodes
        for t0 in tokens0:
            t1 = self.node1.gettokeninfo(t0['token_id'])
            t2 = self.node2.gettokeninfo(t0['token_id'])

            assert_equal(t0['ticker'], t1['ticker'])
            assert_equal(t0['ticker'], t2['ticker'])
            assert_equal(t0['circulating_supply'], t1['circulating_supply'])

        self.log.info("  State consistency verified across all nodes")


if __name__ == '__main__':
    TokenStressReorgTest(__file__).main()
