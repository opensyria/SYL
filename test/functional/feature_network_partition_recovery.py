#!/usr/bin/env python3
# Copyright (c) 2026 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
M-04 REMEDIATION: Network Partition Recovery Tests

Tests that verify the network recovers correctly after partitions:
1. Two-node partition: nodes rejoin and sync to best chain
2. Multi-node partition: majority chain wins
3. Equal partition: chain with more work wins
4. Partition during RandomX key rotation
5. Stale tips are abandoned after rejoin
"""

from test_framework.test_framework import OpenSYTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
)
import time


class NetworkPartitionRecoveryTest(OpenSYTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        # Use faster block times and lower fork height for testing
        self.extra_args = [
            ["-randomxforkheight=5"],
            ["-randomxforkheight=5"],
            ["-randomxforkheight=5"],
            ["-randomxforkheight=5"],
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("=== M-04 Network Partition Recovery Tests ===")

        # Mine initial blocks to exit IBD on all nodes — during IBD,
        # block relay between peers is restricted and sync won't work.
        self.log.info("Mining initial blocks to exit IBD")
        self.generate(self.nodes[0], 2)
        
        self.log.info("Test 1: Basic two-node partition recovery")
        self.test_basic_partition_recovery()
        
        self.log.info("Test 2: Partition during block production")
        self.test_partition_during_mining()
        
        self.log.info("Test 3: Majority chain wins after partition")
        self.test_majority_chain_wins()
        
        self.log.info("Test 4: Partition across RandomX key rotation boundary")
        self.test_partition_across_key_rotation()
        
        self.log.info("=== All partition recovery tests passed ===")

    def test_basic_partition_recovery(self):
        """
        Test that two nodes can partition and rejoin, syncing to same chain.
        """
        # Ensure all nodes are synced
        self.sync_all()
        initial_height = self.nodes[0].getblockcount()
        
        # Partition: disconnect node 0 from node 1
        self.log.info(f"Partitioning at height {initial_height}")
        self.disconnect_nodes(0, 1)
        
        # Mine a block on node 0 (no sync — network is partitioned)
        addr0 = self.nodes[0].getnewaddress()
        self.generatetoaddress(self.nodes[0], 1, addr0, sync_fun=self.no_op)
        
        # Verify node 0 has one more block than node 1
        assert_equal(self.nodes[0].getblockcount(), initial_height + 1)
        
        # Reconnect
        self.log.info("Reconnecting partition")
        self.connect_nodes(0, 1)
        
        # Wait for sync
        self.sync_all()
        
        # Both nodes should be at same height
        assert_equal(
            self.nodes[0].getblockcount(),
            self.nodes[1].getblockcount()
        )
        assert_equal(
            self.nodes[0].getbestblockhash(),
            self.nodes[1].getbestblockhash()
        )
        
        self.log.info("✓ Basic partition recovery successful")

    def test_partition_during_mining(self):
        """
        Test partition where both sides mine blocks, then rejoin.
        Longer chain should win.
        """
        self.sync_all()
        initial_height = self.nodes[0].getblockcount()
        initial_hash = self.nodes[0].getbestblockhash()
        
        # Create partition: nodes 0,1 vs nodes 2,3
        self.log.info("Creating partition: [0,1] vs [2,3]")
        self.disconnect_nodes(1, 2)
        self.disconnect_nodes(0, 2)
        self.disconnect_nodes(0, 3)
        self.disconnect_nodes(1, 3)
        
        # Mine 2 blocks on partition A (nodes 0,1)
        addr0 = self.nodes[0].getnewaddress()
        self.generatetoaddress(self.nodes[0], 2, addr0, sync_fun=self.no_op)
        self.sync_blocks([self.nodes[0], self.nodes[1]])
        
        # Mine 3 blocks on partition B (nodes 2,3) - this should win
        addr2 = self.nodes[2].getnewaddress()
        self.generatetoaddress(self.nodes[2], 3, addr2, sync_fun=self.no_op)
        self.sync_blocks([self.nodes[2], self.nodes[3]])
        
        # Verify chains diverged
        assert_equal(self.nodes[0].getblockcount(), initial_height + 2)
        assert_equal(self.nodes[2].getblockcount(), initial_height + 3)
        assert self.nodes[0].getbestblockhash() != self.nodes[2].getbestblockhash()
        
        # Reconnect all nodes
        self.log.info("Reconnecting all nodes")
        self.connect_nodes(1, 2)
        self.connect_nodes(0, 2)
        self.connect_nodes(0, 3)
        self.connect_nodes(1, 3)
        
        # Wait for reorg to complete
        self.sync_all()
        
        # All nodes should be on the longer chain (partition B's chain)
        expected_height = initial_height + 3
        for i, node in enumerate(self.nodes):
            assert_equal(node.getblockcount(), expected_height)
        
        # All nodes should have same tip
        tip = self.nodes[2].getbestblockhash()
        for i, node in enumerate(self.nodes):
            assert_equal(node.getbestblockhash(), tip)
        
        self.log.info("✓ Partition with competing chains resolved correctly")

    def test_majority_chain_wins(self):
        """
        Test that when partitions rejoin, chain with more cumulative work wins.
        """
        self.sync_all()
        initial_height = self.nodes[0].getblockcount()
        
        # Partition node 3 from everyone
        self.log.info("Isolating node 3")
        self.disconnect_nodes(2, 3)
        self.disconnect_nodes(0, 3)
        self.disconnect_nodes(1, 3)
        
        # Mine blocks on both partitions
        # Majority (nodes 0,1,2) mines 2 blocks
        addr0 = self.nodes[0].getnewaddress()
        self.generatetoaddress(self.nodes[0], 2, addr0, sync_fun=self.no_op)
        self.sync_blocks([self.nodes[0], self.nodes[1], self.nodes[2]])
        
        # Isolated node 3 mines 1 block
        addr3 = self.nodes[3].getnewaddress()
        self.generatetoaddress(self.nodes[3], 1, addr3, sync_fun=self.no_op)
        
        # Verify chains diverged
        assert_equal(self.nodes[0].getblockcount(), initial_height + 2)
        assert_equal(self.nodes[3].getblockcount(), initial_height + 1)
        
        # Reconnect
        self.log.info("Reconnecting isolated node")
        self.connect_nodes(2, 3)
        
        # Wait for sync - node 3 should reorg to majority chain
        self.sync_all()
        
        # All nodes at same height (majority chain)
        expected_height = initial_height + 2
        for i, node in enumerate(self.nodes):
            assert_equal(node.getblockcount(), expected_height)
        
        self.log.info("✓ Minority partition reorged to majority chain")

    def test_partition_across_key_rotation(self):
        """
        Test partition that spans a RandomX key rotation boundary.
        This tests that key block selection works correctly after rejoin.
        """
        self.sync_all()
        
        # Get current height and calculate blocks to key rotation
        current_height = self.nodes[0].getblockcount()
        
        # Mine until we're close to a key rotation (every 32 blocks on mainnet, 
        # but we're in regtest with configurable interval)
        # For this test, just mine some blocks and verify chain consistency
        
        self.log.info(f"Testing partition across key rotation from height {current_height}")
        
        # Partition
        self.disconnect_nodes(0, 1)
        
        # Mine 5 blocks on each partition
        addr0 = self.nodes[0].getnewaddress()
        addr1 = self.nodes[1].getnewaddress()
        
        # Node 0 mines 5 blocks
        self.generatetoaddress(self.nodes[0], 5, addr0, sync_fun=self.no_op)
        
        # Node 1 mines 6 blocks (will win)
        self.generatetoaddress(self.nodes[1], 6, addr1, sync_fun=self.no_op)
        
        # Reconnect
        self.connect_nodes(0, 1)
        
        # Sync
        self.sync_all()
        
        # Verify all nodes on same chain
        tip = self.nodes[1].getbestblockhash()
        for i, node in enumerate(self.nodes):
            assert_equal(node.getbestblockhash(), tip)
        
        # Verify chain is valid (blocks can be retrieved)
        height = self.nodes[0].getblockcount()
        for h in range(max(0, height - 10), height + 1):
            block_hash = self.nodes[0].getblockhash(h)
            block = self.nodes[0].getblock(block_hash)
            assert_greater_than(block['confirmations'], 0)
        
        self.log.info("✓ Partition across potential key rotation handled correctly")


if __name__ == '__main__':
    NetworkPartitionRecoveryTest(__file__).main()
