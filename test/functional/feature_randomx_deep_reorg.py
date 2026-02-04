#!/usr/bin/env python3
# Copyright (c) 2025 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test deep reorg scenarios across multiple RandomX key epochs.

This test implements T-12 from the testing audit:
- Deep reorgs that span multiple key rotation epochs
- Cache rebuild stress during reorg
- Chain validity after epoch-crossing reorg

These tests verify that the node correctly handles reorgs that require
rebuilding RandomX cache/dataset for multiple different keys.
"""

from test_framework.test_framework import OpenSYTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_greater_than_or_equal,
)
import time


class DeepReorgTest(OpenSYTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.rpc_timeout = 1800  # 30 minutes for deep reorg tests
        # Use short key interval for testing
        self.extra_args = [
            ["-randomxforkheight=2", "-randomxkeyinterval=4"],
            ["-randomxforkheight=2", "-randomxkeyinterval=4"],
            ["-randomxforkheight=2", "-randomxkeyinterval=4"],
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Testing deep reorg across multiple key epochs...")
        
        self.test_deep_reorg_multi_epoch()
        self.test_very_deep_reorg()
        self.test_competing_epoch_chains()

    def test_deep_reorg_multi_epoch(self):
        """Test reorg spanning 3+ key epochs."""
        self.log.info("Test 1: Deep reorg across multiple key epochs")
        
        node0 = self.nodes[0]
        node1 = self.nodes[1]
        
        addr0 = node0.getnewaddress()
        addr1 = node1.getnewaddress()
        
        # First, mine enough blocks to get well into RandomX
        self.generatetoaddress(node0, 10, addr0)
        self.sync_blocks()
        
        # Disconnect nodes
        self.disconnect_nodes(0, 1)
        self.disconnect_nodes(0, 2)
        self.disconnect_nodes(1, 2)
        
        start_height = node0.getblockcount()
        
        # Node0 mines 12 blocks (3 key epochs with interval=4)
        self.log.info(f"Node0 mining 12 blocks from height {start_height}")
        self.generatetoaddress(node0, 12, addr0, sync_fun=self.no_op)
        
        # Node1 mines 15 blocks (longer, will win)
        self.log.info(f"Node1 mining 15 blocks from height {start_height}")
        self.generatetoaddress(node1, 15, addr1, sync_fun=self.no_op)
        
        # Verify chains diverged
        assert_equal(node0.getblockcount(), start_height + 12)
        assert_equal(node1.getblockcount(), start_height + 15)
        
        node0_tip = node0.getbestblockhash()
        node1_tip = node1.getbestblockhash()
        
        self.log.info(f"Chains diverged: node0={node0_tip[:16]}... node1={node1_tip[:16]}...")
        
        # Reconnect - should trigger deep reorg
        reorg_start = time.time()
        self.connect_nodes(0, 1)
        self.sync_blocks(self.nodes[0:2])
        reorg_time = time.time() - reorg_start
        
        # Node1's longer chain should win
        assert_equal(node0.getblockcount(), start_height + 15)
        assert_equal(node0.getbestblockhash(), node1.getbestblockhash())
        
        self.log.info(f"Deep reorg completed in {reorg_time:.2f}s")
        
        # Reconnect all and sync to ensure clean state for next test
        self.connect_nodes(0, 2)
        self.connect_nodes(1, 2)
        self.sync_blocks()

    def test_very_deep_reorg(self):
        """Test very deep reorg (20+ blocks across 5+ epochs)."""
        self.log.info("Test 2: Very deep reorg")
        
        node0 = self.nodes[0]
        node1 = self.nodes[1]
        
        # Ensure all nodes are synced before starting
        self.sync_blocks()
        start_height = node0.getblockcount()
        
        # Disconnect
        self.disconnect_nodes(0, 1)
        self.disconnect_nodes(0, 2)
        self.disconnect_nodes(1, 2)
        
        addr0 = node0.getnewaddress()
        addr1 = node1.getnewaddress()
        
        # Node0 mines 20 blocks
        self.generatetoaddress(node0, 20, addr0, sync_fun=self.no_op)
        
        # Node1 mines 25 blocks
        self.generatetoaddress(node1, 25, addr1, sync_fun=self.no_op)
        
        self.log.info(f"Node0 at {node0.getblockcount()}, Node1 at {node1.getblockcount()}")
        
        # Reconnect node0 and node1 only
        self.connect_nodes(0, 1)
        self.sync_blocks(self.nodes[0:2])
        
        # Verify reorg success
        assert_equal(node0.getblockcount(), start_height + 25)
        assert_equal(node0.getbestblockhash(), node1.getbestblockhash())
        
        self.log.info("Very deep reorg successful")
        
        # Reconnect all for next test
        self.connect_nodes(0, 2)
        self.connect_nodes(1, 2)
        self.sync_blocks()

    def test_competing_epoch_chains(self):
        """Test three competing chains across different epochs."""
        self.log.info("Test 3: Three competing epoch chains")
        
        node0 = self.nodes[0]
        node1 = self.nodes[1]
        node2 = self.nodes[2]
        
        # Sync all
        self.sync_blocks()
        
        # Disconnect all
        self.disconnect_nodes(0, 1)
        self.disconnect_nodes(0, 2)
        self.disconnect_nodes(1, 2)
        
        addr0 = node0.getnewaddress()
        addr1 = node1.getnewaddress()
        addr2 = node2.getnewaddress()
        
        start_height = node0.getblockcount()
        
        # Each node mines different amount
        self.generatetoaddress(node0, 8, addr0, sync_fun=self.no_op)   # 2 epochs
        self.generatetoaddress(node1, 12, addr1, sync_fun=self.no_op)  # 3 epochs
        self.generatetoaddress(node2, 16, addr2, sync_fun=self.no_op)  # 4 epochs (winner)
        
        # Reconnect all at once
        self.connect_nodes(0, 1)
        self.connect_nodes(1, 2)
        self.connect_nodes(0, 2)
        
        self.sync_blocks()
        
        # Node2's chain should win
        expected_height = start_height + 16
        assert_equal(node0.getblockcount(), expected_height)
        assert_equal(node1.getblockcount(), expected_height)
        assert_equal(node2.getblockcount(), expected_height)
        
        # All should have same tip
        tip = node2.getbestblockhash()
        assert_equal(node0.getbestblockhash(), tip)
        assert_equal(node1.getbestblockhash(), tip)
        
        self.log.info(f"Three-way reorg resolved to height {expected_height}")


if __name__ == '__main__':
    DeepReorgTest(__file__).main()
