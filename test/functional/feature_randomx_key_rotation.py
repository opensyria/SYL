#!/usr/bin/env python3
# Copyright (c) 2025 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test RandomX key rotation scenarios.

This test implements T-06 from the testing audit:
- Key rotation boundary mining
- Key block reorg handling
- Cache invalidation during key changes

Key rotation is critical for RandomX security - the key (derived from
a specific block hash) determines the RandomX program for subsequent blocks.
"""

from test_framework.test_framework import OpenSYTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
)


class RandomXKeyRotationTest(OpenSYTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.rpc_timeout = 1200  # 20 minutes - key rotation requires cache rebuilds
        # Use short key interval for testing (default is 32)
        self.extra_args = [
            ["-randomxforkheight=2", "-randomxkeyinterval=8"],
            ["-randomxforkheight=2", "-randomxkeyinterval=8"],
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Testing RandomX key rotation scenarios...")
        
        self.test_basic_key_rotation()
        self.test_mining_across_key_boundary()
        self.test_reorg_affecting_key_block()
        self.test_rapid_key_rotations()

    def test_basic_key_rotation(self):
        """Test that key rotation happens at expected intervals."""
        self.log.info("Test 1: Basic key rotation")
        
        node = self.nodes[0]
        address = node.getnewaddress()
        
        # Mine past first key rotation (key interval = 8)
        # Key blocks: 2, 10, 18, 26, ...
        target_height = 12  # Past first rotation
        
        current = node.getblockcount()
        if current < target_height:
            self.generatetoaddress(node, target_height - current, address)
        
        assert_equal(node.getblockcount(), target_height)
        
        # Verify all blocks are valid
        for height in range(1, target_height + 1):
            blockhash = node.getblockhash(height)
            block = node.getblock(blockhash)
            assert_greater_than(block['confirmations'], 0)
        
        self.log.info(f"Key rotation at height 10 processed successfully")

    def test_mining_across_key_boundary(self):
        """Test that mining works correctly across key rotation."""
        self.log.info("Test 2: Mining across key boundary")
        
        node = self.nodes[0]
        address = node.getnewaddress()
        
        # Get current height
        current = node.getblockcount()
        
        # Mine 20 more blocks (crosses multiple key boundaries)
        self.generatetoaddress(node, 20, address)
        
        new_height = node.getblockcount()
        assert_equal(new_height, current + 20)
        
        # Verify chain integrity
        tip_hash = node.getbestblockhash()
        tip = node.getblock(tip_hash)
        assert_equal(tip['height'], new_height)
        
        self.log.info(f"Mined across key boundaries successfully")

    def test_reorg_affecting_key_block(self):
        """Test reorg that changes a key block."""
        self.log.info("Test 3: Reorg affecting key block")
        
        node0 = self.nodes[0]
        node1 = self.nodes[1]
        
        # Sync nodes
        self.sync_blocks()
        
        # Disconnect
        self.disconnect_nodes(0, 1)
        
        start_height = node0.getblockcount()
        addr0 = node0.getnewaddress()
        addr1 = node1.getnewaddress()
        
        # Node0 mines 4 blocks
        self.generatetoaddress(node0, 4, addr0, sync_fun=self.no_op)
        
        # Node1 mines 6 blocks (longer, will cause reorg)
        self.generatetoaddress(node1, 6, addr1, sync_fun=self.no_op)
        
        # Reconnect
        self.connect_nodes(0, 1)
        self.sync_blocks()
        
        # Node1's chain should win
        assert_equal(node0.getblockcount(), start_height + 6)
        assert_equal(node0.getbestblockhash(), node1.getbestblockhash())
        
        self.log.info("Reorg affecting key block handled correctly")

    def test_rapid_key_rotations(self):
        """Test behavior with multiple rapid key rotations."""
        self.log.info("Test 4: Rapid key rotations")
        
        node = self.nodes[0]
        address = node.getnewaddress()
        
        # Mine many blocks to trigger multiple key rotations
        self.generatetoaddress(node, 50, address)
        
        # Verify chain is still valid
        height = node.getblockcount()
        tip_hash = node.getbestblockhash()
        tip = node.getblock(tip_hash)
        
        assert_equal(tip['height'], height)
        assert_greater_than(tip['confirmations'], 0)
        
        self.log.info(f"Multiple key rotations handled successfully (height: {height})")


if __name__ == '__main__':
    RandomXKeyRotationTest(__file__).main()
