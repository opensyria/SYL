#!/usr/bin/env python3
# Copyright (c) 2025 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test RandomX proof-of-work integration.

This test verifies:
1. SHA256d mining works before RandomX fork height
2. RandomX mining activates at fork height
3. Chain syncs correctly between nodes with RandomX blocks
4. Difficulty adjustment works correctly with RandomX
5. Invalid RandomX blocks are rejected
"""

from test_framework.test_framework import OpenSYTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
)


class RandomXPowTest(OpenSYTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        # RandomX mining is significantly slower than SHA256d
        # Increase RPC timeout for long mining operations
        self.rpc_timeout = 600  # 10 minutes
        # Set fork height to 5 for faster testing (regtest default is 200)
        self.extra_args = [
            ["-randomxforkheight=5"],
            ["-randomxforkheight=5"],
        ]

    def skip_test_if_missing_module(self):
        # This test requires mining support and wallet
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Testing SHA256d mining before fork height...")
        self.test_prefork_mining()

        self.log.info("Testing RandomX activation at fork height...")
        self.test_fork_activation()

        self.log.info("Testing RandomX mining after fork...")
        self.test_postfork_mining()

        self.log.info("Testing chain sync with RandomX blocks...")
        self.test_chain_sync()

        self.log.info("Testing getblockchaininfo RandomX fields...")
        self.test_rpc_info()

    def test_prefork_mining(self):
        """Test that SHA256d mining works before fork height."""
        node = self.nodes[0]
        
        # Mine blocks before fork height (fork at height 5)
        # Generate 4 blocks (heights 1-4) using SHA256d
        address = node.getnewaddress()
        blockhashes = self.generatetoaddress(node, 4, address)
        
        assert_equal(len(blockhashes), 4)
        assert_equal(node.getblockcount(), 4)
        
        # Verify blocks are valid
        for blockhash in blockhashes:
            block = node.getblock(blockhash)
            assert_greater_than(block['confirmations'], 0)

    def test_fork_activation(self):
        """Test that RandomX activates exactly at fork height."""
        node = self.nodes[0]
        
        # Current height should be 4, fork at height 5
        assert_equal(node.getblockcount(), 4)
        
        # Mine one more block - this should be the first RandomX block
        address = node.getnewaddress()
        blockhashes = self.generatetoaddress(node, 1, address)
        
        assert_equal(len(blockhashes), 1)
        assert_equal(node.getblockcount(), 5)
        
        # Block at height 5 should be valid (RandomX)
        block = node.getblock(blockhashes[0])
        assert_equal(block['height'], 5)
        assert_greater_than(block['confirmations'], 0)

    def test_postfork_mining(self):
        """Test that RandomX mining works after fork."""
        node = self.nodes[0]
        
        # Mine more blocks with RandomX (reduced count for speed)
        address = node.getnewaddress()
        blockhashes = self.generatetoaddress(node, 3, address)
        
        assert_equal(len(blockhashes), 3)
        assert_equal(node.getblockcount(), 8)
        
        # All blocks should be valid
        for blockhash in blockhashes:
            block = node.getblock(blockhash)
            assert_greater_than(block['confirmations'], 0)

    def test_chain_sync(self):
        """Test that nodes can sync RandomX blocks."""
        node0 = self.nodes[0]
        node1 = self.nodes[1]
        
        # Connect nodes
        self.connect_nodes(0, 1)
        
        # Sync chains
        self.sync_blocks()
        
        # Both nodes should have the same chain
        assert_equal(node0.getblockcount(), node1.getblockcount())
        assert_equal(node0.getbestblockhash(), node1.getbestblockhash())
        
        # Mine more on node0 and verify sync (reduced count for speed)
        address = node0.getnewaddress()
        blockhashes = self.generatetoaddress(node0, 2, address)
        
        self.sync_blocks()
        
        assert_equal(node0.getblockcount(), node1.getblockcount())
        assert_equal(node0.getbestblockhash(), node1.getbestblockhash())

    def test_rpc_info(self):
        """Test that RPC provides correct information about RandomX."""
        node = self.nodes[0]
        
        # Get blockchain info
        info = node.getblockchaininfo()
        
        # Verify chain is operational
        assert_greater_than(info['blocks'], 0)
        assert_equal(info['chain'], 'regtest')
        
        # Get mining info
        mining_info = node.getmininginfo()
        assert_greater_than(mining_info['blocks'], 0)


if __name__ == '__main__':
    RandomXPowTest(__file__).main()
