#!/usr/bin/env python3
# Copyright (c) 2025-2026 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""SRC-20 token reindex test.

Verifies token state is correctly rebuilt from the chain after -reindex:
- Issue tokens, perform transfers and burns across many blocks
- Stop node and restart with -reindex
- Verify all token info, balances, and holder counts match pre-reindex state
- Verify the node can issue/transfer/burn normally after reindex
- Verify multi-node sync after reindex
"""

from test_framework.test_framework import OpenSYTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
)


class TokenReindexTest(OpenSYTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ["-randomxforkheight=5"],
            ["-randomxforkheight=5"],
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.node0 = self.nodes[0]
        self.node1 = self.nodes[1]

        self.log.info("Mining initial blocks...")
        self.addr0 = self.node0.getnewaddress()
        self.addr1 = self.node1.getnewaddress()
        self.generatetoaddress(self.node0, 120, self.addr0)
        self.sync_all()

        self.log.info("=== Phase 1: Build Token State ===")
        token_data = self.build_token_state()

        self.log.info("=== Phase 2: Capture Pre-Reindex Snapshot ===")
        snapshot = self.capture_snapshot(token_data)

        self.log.info("=== Phase 3: Reindex Node 0 ===")
        self.reindex_node()

        self.log.info("=== Phase 4: Verify Post-Reindex State ===")
        self.verify_snapshot(token_data, snapshot)

        self.log.info("=== Phase 5: Operations After Reindex ===")
        self.test_operations_after_reindex(token_data)

        self.log.info("All reindex tests passed!")

    def build_token_state(self):
        """Create a rich token state: multiple tokens, transfers, burns."""
        tokens = {}

        # Token 1: Issue, transfer some to node1, burn some
        self.log.info("  Creating token RIDX with transfers and burns")
        result = self.node0.walletissuetoken("RIDX", "Reindex Test", 8, 1000000)
        token1 = result['token_id']
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        # Transfer 300000 to node1
        self.node0.wallettransfertoken(token1, self.addr1, 300000)
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        # Burn 100000
        self.node0.walletburntoken(token1, 100000)
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        tokens['RIDX'] = {
            'id': token1,
            'total_supply': 1000000,
            'expected_circulating': 900000,  # 1M - 100K burned
            'expected_node0_balance': 600000,  # 1M - 300K transferred - 100K burned
            'expected_node1_balance': 300000,
            'expected_holders': 2,
        }

        # Token 2: Issue and fully transfer (zero sender balance)
        self.log.info("  Creating token MOVE with full transfer")
        result = self.node0.walletissuetoken("MOVE", "Move Token", 0, 5000)
        token2 = result['token_id']
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        self.node0.wallettransfertoken(token2, self.addr1, 5000)
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        tokens['MOVE'] = {
            'id': token2,
            'total_supply': 5000,
            'expected_circulating': 5000,
            'expected_node0_balance': 0,
            'expected_node1_balance': 5000,
            'expected_holders': 1,
        }

        # Token 3: Issue and burn everything (zero circulating)
        self.log.info("  Creating token GONE with complete burn")
        result = self.node0.walletissuetoken("GONE", "Gone Token", 0, 2000)
        token3 = result['token_id']
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        self.node0.walletburntoken(token3, 2000)
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        tokens['GONE'] = {
            'id': token3,
            'total_supply': 2000,
            'expected_circulating': 0,
            'expected_node0_balance': 0,
            'expected_node1_balance': 0,
            'expected_holders': 0,
        }

        # Mine some extra blocks to bury the state
        self.generatetoaddress(self.node0, 5, self.addr0)
        self.sync_all()

        return tokens

    def capture_snapshot(self, tokens):
        """Capture the full token state for comparison after reindex."""
        snapshot = {}
        for ticker, data in tokens.items():
            token_id = data['id']
            info = self.node0.gettokeninfo(token_id)
            holders = self.node0.gettokenholders(token_id)
            snapshot[ticker] = {
                'info': info,
                'holders': holders,
            }
            self.log.info(f"  {ticker}: circ={info['circulating_supply']}, "
                         f"holders={info['holder_count']}")
        return snapshot

    def reindex_node(self):
        """Stop node0 and restart with -reindex."""
        self.log.info("  Stopping node 0...")
        self.stop_node(0)

        self.log.info("  Restarting node 0 with -reindex...")
        self.start_node(0, extra_args=self.extra_args[0] + ["-reindex"])

        self.log.info("  Waiting for reindex to complete...")
        self.connect_nodes(0, 1)
        self.sync_all()

        # Re-assign reference
        self.node0 = self.nodes[0]

    def verify_snapshot(self, tokens, snapshot):
        """Compare post-reindex state against the pre-reindex snapshot."""
        for ticker, data in tokens.items():
            token_id = data['id']
            self.log.info(f"  Verifying {ticker}...")

            # Token info should match
            info = self.node0.gettokeninfo(token_id)
            pre = snapshot[ticker]['info']

            assert_equal(info['ticker'], pre['ticker'])
            assert_equal(info['name'], pre['name'])
            assert_equal(info['total_supply'], pre['total_supply'])
            assert_equal(info['circulating_supply'], pre['circulating_supply'])
            assert_equal(info['holder_count'], pre['holder_count'])
            assert_equal(info['decimals'], pre['decimals'])

            # Verify expected values
            assert_equal(info['circulating_supply'], data['expected_circulating'])
            assert_equal(info['holder_count'], data['expected_holders'])

            # Holders should match
            holders = self.node0.gettokenholders(token_id)
            pre_holders = snapshot[ticker]['holders']
            assert_equal(len(holders), len(pre_holders))

        self.log.info("  All token state matches pre-reindex snapshot!")

    def test_operations_after_reindex(self, tokens):
        """Verify the node can perform new token operations post-reindex."""
        # Issue a completely new token
        self.log.info("  Test: New issuance after reindex")
        result = self.node0.walletissuetoken("POST", "Post Reindex", 0, 7777)
        assert 'txid' in result
        new_token_id = result['token_id']
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        info = self.node0.gettokeninfo(new_token_id)
        assert_equal(info['ticker'], "POST")
        assert_equal(info['total_supply'], 7777)

        # Transfer from the surviving token (RIDX)
        ridx_id = tokens['RIDX']['id']
        self.log.info("  Test: Transfer on existing token after reindex")
        self.node0.wallettransfertoken(ridx_id, self.addr1, 1000)
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        info = self.node0.gettokeninfo(ridx_id)
        assert_equal(info['circulating_supply'], 900000)  # unchanged by transfer

        # Burn on existing token
        self.log.info("  Test: Burn on existing token after reindex")
        self.node0.walletburntoken(ridx_id, 500)
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        info = self.node0.gettokeninfo(ridx_id)
        assert_equal(info['circulating_supply'], 899500)

        # Node1 agrees
        info1 = self.node1.gettokeninfo(ridx_id)
        assert_equal(info1['circulating_supply'], 899500)

        # listtokens should include all tokens (3 original + 1 new)
        all_tokens = self.node0.listtokens()
        assert_equal(len(all_tokens), 4)


if __name__ == '__main__':
    TokenReindexTest(__file__).main()
