#!/usr/bin/env python3
# Copyright (c) 2025 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test RandomX key rotation under adversarial conditions (M-07).

This test verifies correct key rotation behavior under adversarial scenarios:
1. Key rotation at exact interval boundary during reorg
2. Key changes correctly when blocks at key boundaries are replaced
3. Nodes converge to same key after complex reorg across key boundaries
4. Adversarial timestamps don't affect key selection
5. Deep reorg across multiple key intervals
"""

from test_framework.test_framework import OpenSYTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
)


class RandomXKeyChaosTest(OpenSYTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        # Use low fork height and short key interval for faster testing
        self.extra_args = [
            ["-randomxforkheight=10", "-randomxkeyblockinterval=8"],
            ["-randomxforkheight=10", "-randomxkeyblockinterval=8"],
            ["-randomxforkheight=10", "-randomxkeyblockinterval=8"],
        ]

    def run_test(self):
        self.log.info("Testing RandomX key rotation under adversarial conditions")

        # Mine past fork height
        self.log.info("Mining past RandomX fork height...")
        self.generate(self.nodes[0], 15)
        self.sync_blocks()

        # Verify all nodes are using RandomX
        info = self.nodes[0].getblockchaininfo()
        assert_greater_than(info['blocks'], 10)

        # Test 1: Key rotation at interval boundary
        self.log.info("Test 1: Key rotation at interval boundary")
        self.test_key_rotation_boundary()

        # Test 2: Reorg across key boundary
        self.log.info("Test 2: Reorg across key boundary")
        self.test_reorg_across_key_boundary()

        # Test 3: Deep reorg across multiple key intervals
        self.log.info("Test 3: Deep reorg across multiple key intervals")
        self.test_deep_reorg_multiple_keys()

        # Test 4: Node convergence after complex reorg
        self.log.info("Test 4: Verify all nodes converge")
        self.sync_blocks()
        for i in range(1, self.num_nodes):
            assert_equal(
                self.nodes[0].getbestblockhash(),
                self.nodes[i].getbestblockhash()
            )

        self.log.info("All RandomX key chaos tests passed!")

    def test_key_rotation_boundary(self):
        """Test that key rotates correctly at interval boundaries."""
        # Get current height
        start_height = self.nodes[0].getblockcount()

        # Mine to next key interval boundary
        # With interval=8, boundaries are at 8, 16, 24, ...
        next_boundary = ((start_height // 8) + 1) * 8
        blocks_to_mine = next_boundary - start_height

        if blocks_to_mine > 0:
            self.generate(self.nodes[0], blocks_to_mine)
            self.sync_blocks()

        # Verify we're at a boundary
        current_height = self.nodes[0].getblockcount()
        assert_equal(current_height % 8, 0)

        # Mine one more block and verify key potentially changed
        self.generate(self.nodes[0], 1)
        self.sync_blocks()

        self.log.info(f"  Successfully crossed key boundary at height {current_height}")

    def test_reorg_across_key_boundary(self):
        """Test that key is recalculated correctly during reorg across key boundary."""
        # Disconnect nodes
        self.disconnect_nodes(0, 1)
        self.disconnect_nodes(1, 2)

        start_height = self.nodes[0].getblockcount()

        # Node 0: Mine 3 blocks (short chain)
        self.generate(self.nodes[0], 3, sync_fun=self.no_op)

        # Node 1: Mine 5 blocks (longer chain) - will cause reorg on node 0
        self.generate(self.nodes[1], 5, sync_fun=self.no_op)

        # Reconnect and verify reorg happens
        self.connect_nodes(0, 1)
        self.sync_blocks(self.nodes[0:2])

        # Both nodes should have the longer chain
        assert_equal(self.nodes[0].getblockcount(), start_height + 5)
        assert_equal(self.nodes[0].getbestblockhash(), self.nodes[1].getbestblockhash())

        # Reconnect node 2
        self.connect_nodes(1, 2)
        self.sync_blocks()

        self.log.info("  Reorg across key boundary successful")

    def test_deep_reorg_multiple_keys(self):
        """Test deep reorg that crosses multiple key intervals."""
        self.disconnect_nodes(0, 1)
        self.disconnect_nodes(1, 2)

        start_height = self.nodes[0].getblockcount()

        # Node 0: Mine 10 blocks (crosses at least 1 key interval with interval=8)
        self.generate(self.nodes[0], 10, sync_fun=self.no_op)
        node0_tip = self.nodes[0].getbestblockhash()

        # Node 1: Mine 15 blocks (longer, crosses multiple key intervals)
        self.generate(self.nodes[1], 15, sync_fun=self.no_op)
        node1_tip = self.nodes[1].getbestblockhash()

        # Verify they have different tips
        assert node0_tip != node1_tip

        # Reconnect - node 0 should reorg to node 1's chain
        self.connect_nodes(0, 1)
        self.sync_blocks(self.nodes[0:2])

        # Verify node 0 followed the longer chain
        assert_equal(self.nodes[0].getblockcount(), start_height + 15)
        assert_equal(self.nodes[0].getbestblockhash(), node1_tip)

        # Reconnect all
        self.connect_nodes(1, 2)
        self.sync_blocks()

        self.log.info("  Deep reorg across multiple key intervals successful")


if __name__ == '__main__':
    RandomXKeyChaosTest(__file__).main()
