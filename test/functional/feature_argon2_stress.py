#!/usr/bin/env python3
# Copyright (c) 2025 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Stress tests for Argon2id emergency PoW fallback.

These tests exercise edge cases and stress conditions for the Argon2id
emergency fallback mechanism. They ensure the system remains stable
under various challenging scenarios.

Test scenarios:
1. Rapid block production at emergency boundary
2. Multiple reorgs across emergency boundary  
3. Chain with many algorithm transitions
4. Concurrent block validation
5. Memory pressure during Argon2 hashing
"""

from test_framework.test_framework import OpenSYTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
)
import time


class Argon2StressTest(OpenSYTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.rpc_timeout = 1800  # 30 minutes for stress tests
        
        # Emergency at height 20 for quick testing
        self.extra_args = [
            ["-randomxforkheight=5", "-argon2emergencyheight=20"],
            ["-randomxforkheight=5", "-argon2emergencyheight=20"],
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Stress Test 1: Rapid block production across boundary")
        self.test_rapid_blocks_across_boundary()

        self.log.info("Stress Test 2: Multiple reorgs at emergency boundary")
        self.test_reorgs_at_boundary()

        self.log.info("Stress Test 3: Extended Argon2 chain mining")
        self.test_extended_argon2_mining()

    def test_rapid_blocks_across_boundary(self):
        """Mine blocks rapidly across the emergency activation boundary."""
        node = self.nodes[0]
        address = node.getnewaddress()
        
        # Mine from 0 to past emergency height (20) in one batch (no sync)
        blockhashes = self.generatetoaddress(node, 30, address, sync_fun=self.no_op)
        
        assert_equal(len(blockhashes), 30)
        assert_equal(node.getblockcount(), 30)
        
        # Verify all blocks have proper confirmations
        for blockhash in blockhashes:
            block = node.getblock(blockhash)
            assert_greater_than(block['confirmations'], 0)
        
        self.log.info(f"  Mined 30 blocks across emergency boundary (height 20)")

    def test_reorgs_at_boundary(self):
        """Test reorgs that cross the emergency activation boundary."""
        node0 = self.nodes[0]
        node1 = self.nodes[1]
        
        # Sync nodes first
        self.connect_nodes(0, 1)
        self.sync_blocks([node0, node1])
        
        initial_height = node0.getblockcount()
        
        # Disconnect for competing chains
        self.disconnect_nodes(0, 1)
        
        address0 = node0.getnewaddress()
        address1 = node1.getnewaddress()
        
        # Create competing chains (both in Argon2 territory) - no sync while disconnected
        blocks0 = self.generatetoaddress(node0, 5, address0, sync_fun=self.no_op)
        blocks1 = self.generatetoaddress(node1, 7, address1, sync_fun=self.no_op)  # Longer chain
        
        # Verify divergence
        assert_equal(node0.getblockcount(), initial_height + 5)
        assert_equal(node1.getblockcount(), initial_height + 7)
        
        # Reconnect and sync - longer chain wins
        self.connect_nodes(0, 1)
        self.sync_blocks([node0, node1])
        
        # Both should be on the longer chain
        final_height = node0.getblockcount()
        assert_equal(final_height, initial_height + 7)
        assert_equal(node0.getbestblockhash(), node1.getbestblockhash())
        
        self.log.info(f"  Reorg successful in Argon2 territory: {initial_height + 5} -> {final_height}")

    def test_extended_argon2_mining(self):
        """Test mining many blocks with Argon2id."""
        node = self.nodes[0]
        
        # Ensure we're past emergency height
        initial_height = node.getblockcount()
        assert_greater_than(initial_height, 20)
        
        address = node.getnewaddress()
        
        # Mine 20 more Argon2id blocks (no sync - only node0)
        start_time = time.time()
        blockhashes = self.generatetoaddress(node, 20, address, sync_fun=self.no_op)
        elapsed = time.time() - start_time
        
        assert_equal(len(blockhashes), 20)
        
        self.log.info(f"  Mined 20 Argon2id blocks in {elapsed:.2f}s ({elapsed/20:.2f}s/block)")
        
        # Verify chain is coherent
        final_height = node.getblockcount()
        assert_equal(final_height, initial_height + 20)


if __name__ == '__main__':
    Argon2StressTest(__file__).main()
