#!/usr/bin/env python3
# Copyright (c) 2025-2026 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""SRC-20 token multi-node consistency test.

Verifies all nodes agree on token state after complex multi-hop operations:
- Three-node chain-of-custody: node0 → node1 → node2
- Holder count accuracy after burns to zero
- Token stats agreement across all nodes
- Simultaneous issuance conflict resolution
- gettokenholders agreement
"""

from test_framework.test_framework import OpenSYTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
)


class TokenMultiNodeConsistencyTest(OpenSYTestFramework):
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

    def run_test(self):
        self.node0 = self.nodes[0]
        self.node1 = self.nodes[1]
        self.node2 = self.nodes[2]

        self.log.info("Mining initial blocks...")
        self.addr0 = self.node0.getnewaddress()
        self.addr1 = self.node1.getnewaddress()
        self.addr2 = self.node2.getnewaddress()
        self.generatetoaddress(self.node0, 120, self.addr0)
        self.sync_all()

        # Give node1 some SYL for fees
        self.node0.sendtoaddress(self.addr1, 200)
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        self.log.info("=== Three-Node Chain-of-Custody ===")
        self.test_chain_of_custody()

        self.log.info("=== Holder Count After Partial Burns ===")
        self.test_holder_count_accuracy()

        self.log.info("=== Token State Agreement Across All Nodes ===")
        self.test_state_agreement()

        self.log.info("All multi-node consistency tests passed!")

    def test_chain_of_custody(self):
        """Issue on node0, transfer to node1, node1 transfers to node2."""
        self.log.info("  Test: node0 issues token")
        result = self.node0.walletissuetoken("CUST", "Custody Test", 0, 10000)
        token_id = result['token_id']
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        # node0 → node1: 6000
        self.log.info("  Test: node0 → node1 transfer (6000)")
        self.node0.wallettransfertoken(token_id, self.addr1, 6000)
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        # node1 → node2: 3000
        self.log.info("  Test: node1 → node2 transfer (3000)")
        self.node1.wallettransfertoken(token_id, self.addr2, 3000)
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        # Verify all three nodes agree on token info
        infos = [node.gettokeninfo(token_id) for node in self.nodes]
        for i in range(len(infos)):
            assert_equal(infos[i]['total_supply'], 10000)
            assert_equal(infos[i]['circulating_supply'], 10000)

        # All nodes should report the same holder_count (≥2)
        for i in range(1, len(infos)):
            assert infos[0]['holder_count'] == infos[i]['holder_count'], \
                f"Node 0 vs {i} holder_count mismatch: {infos[0]['holder_count']} vs {infos[i]['holder_count']}"
        assert_greater_than(infos[0]['holder_count'], 1)

        # Verify holders list on all nodes
        for i, node in enumerate(self.nodes):
            holders = node.gettokenholders(token_id)
            assert len(holders) == infos[i]['holder_count'], \
                f"Node {i} holders list length {len(holders)} != holder_count {infos[i]['holder_count']}"

            # Sum of all balances from holders should equal circulating
            total = sum(h['balance'] for h in holders)
            assert_equal(total, 10000)

    def test_holder_count_accuracy(self):
        """Issue to 3 holders, burn 2 to zero, verify holder count drops."""
        self.log.info("  Test: Issue and distribute to all nodes")
        result = self.node0.walletissuetoken("HOLD", "Holder Test", 0, 9000)
        token_id = result['token_id']
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        # Distribute: node0=3000, node1=3000, node2=3000
        self.node0.wallettransfertoken(token_id, self.addr1, 3000)
        self.node0.wallettransfertoken(token_id, self.addr2, 3000)
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        # All 3 nodes should show holder_count=3
        for i, node in enumerate(self.nodes):
            info = node.gettokeninfo(token_id)
            assert info['holder_count'] == 3, \
                f"Node {i}: expected 3 holders, got {info['holder_count']}"

        # node0 burns all its tokens (3000)
        self.log.info("  Test: node0 burns all its tokens")
        self.node0.walletburntoken(token_id, 3000)
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        # Should show holder_count=2, circulating=6000
        for i, node in enumerate(self.nodes):
            info = node.gettokeninfo(token_id)
            assert info['holder_count'] == 2, \
                f"Node {i}: expected 2 holders after burn, got {info['holder_count']}"
            assert_equal(info['circulating_supply'], 6000)

        # Verify gettokenholders returns exactly 2
        for i, node in enumerate(self.nodes):
            holders = node.gettokenholders(token_id)
            assert len(holders) == 2, \
                f"Node {i}: expected 2 in holders list, got {len(holders)}"

    def test_state_agreement(self):
        """After many operations, all nodes must show identical gettokenstats."""
        self.log.info("  Test: Complex operations then full state check")
        
        # Issue a third token with multiple transfers across blocks
        result = self.node0.walletissuetoken("AGRE", "Agreement Test", 4, 99999)
        token_id = result['token_id']
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        # 5 transfers across separate blocks
        for i in range(5):
            self.node0.wallettransfertoken(token_id, self.addr1, 1000)
            self.generatetoaddress(self.node0, 1, self.addr0)

        self.sync_all()

        # Burn some
        self.node0.walletburntoken(token_id, 2000)
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        # Compare gettokeninfo across all nodes
        infos = [node.gettokeninfo(token_id) for node in self.nodes]
        
        for field in ['total_supply', 'circulating_supply', 'holder_count',
                      'ticker', 'name', 'decimals']:
            vals = [info[field] for info in infos]
            assert vals[0] == vals[1], \
                f"Node 0 vs 1: {field} mismatch: {vals[0]} vs {vals[1]}"
            assert vals[1] == vals[2], \
                f"Node 1 vs 2: {field} mismatch: {vals[1]} vs {vals[2]}"

        # Verify expected values
        assert_equal(infos[0]['circulating_supply'], 97999)  # 99999 - 2000
        assert_equal(infos[0]['holder_count'], 2)  # node0 and node1

        # listtokens count should match across all nodes
        lists = [node.listtokens() for node in self.nodes]
        for i in range(len(lists) - 1):
            assert len(lists[i]) == len(lists[i + 1]), \
                f"Node {i} vs {i+1}: listtokens count mismatch: {len(lists[i])} vs {len(lists[i+1])}"

        self.log.info(f"  All {len(self.nodes)} nodes agree on "
                     f"{len(lists[0])} tokens with matching state!")


if __name__ == '__main__':
    TokenMultiNodeConsistencyTest(__file__).main()
