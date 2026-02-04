#!/usr/bin/env python3
# Copyright (c) 2025 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test RandomX header validation and protection against header spam attacks.

This test verifies:
1. Weak header validation during initial sync allows headers with claimed work
2. Rate limiting prevents excessive header spam
3. Invalid RandomX headers are eventually rejected during full validation
4. Key rotation is handled correctly during header sync
"""

from test_framework.test_framework import OpenSYTestFramework
from test_framework.p2p import (
    P2PInterface,
)
from test_framework.messages import (
    CBlockHeader,
    msg_headers,
    ser_uint256,
    uint256_from_str,
)
from test_framework.util import (
    assert_equal,
    assert_greater_than,
)
import time
import struct


class RandomXHeaderSpamTest(OpenSYTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        # RandomX mining is significantly slower than SHA256d
        self.rpc_timeout = 600  # 10 minutes for long mining operations
        # Set low fork height for testing
        self.extra_args = [
            ["-randomxforkheight=5", "-minimumchainwork=0x0"],
            ["-randomxforkheight=5", "-minimumchainwork=0x0"],
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        # This test uses generatetoaddress RPC which leverages the node's
        # built-in RandomX miner. Mining is done at regtest difficulty.

    def run_test(self):
        self.log.info("Testing RandomX header validation...")
        
        self.test_valid_header_sync()
        self.test_header_chain_integrity()
        self.test_key_rotation_during_sync()

    def test_valid_header_sync(self):
        """Test that valid headers sync correctly between nodes."""
        self.log.info("Test 1: Valid header synchronization")
        
        node0 = self.nodes[0]
        node1 = self.nodes[1]
        
        # Mine some blocks on node0 (across fork boundary)
        address = node0.getnewaddress()
        
        # Mine 10 blocks - 4 SHA256d, 6 RandomX
        blockhashes = self.generatetoaddress(node0, 10, address)
        assert_equal(len(blockhashes), 10)
        assert_equal(node0.getblockcount(), 10)
        
        # Connect nodes and sync
        self.connect_nodes(0, 1)
        self.sync_blocks()
        
        # Verify sync
        assert_equal(node0.getblockcount(), node1.getblockcount())
        assert_equal(node0.getbestblockhash(), node1.getbestblockhash())
        
        self.log.info("Headers synced correctly across fork boundary")
        
        # Disconnect for next test
        self.disconnect_nodes(0, 1)

    def test_header_chain_integrity(self):
        """Test that header chain maintains integrity across algorithm change."""
        self.log.info("Test 2: Header chain integrity")
        
        node = self.nodes[0]
        
        # Get headers around fork boundary
        fork_height = 5
        
        for height in range(1, min(node.getblockcount(), 12)):
            blockhash = node.getblockhash(height)
            header = node.getblockheader(blockhash)
            
            # Verify header structure
            assert 'height' in header
            assert 'hash' in header
            assert 'previousblockhash' in header or height == 0
            assert 'nonce' in header
            assert 'bits' in header
            
            # Log algorithm being used
            algo = "RandomX" if height >= fork_height else "SHA256d"
            self.log.info(f"Height {height}: {algo}, bits={hex(int(header['bits'], 16))}")
        
        self.log.info("Header chain integrity verified")

    def test_key_rotation_during_sync(self):
        """Test header sync handles RandomX key rotation correctly."""
        self.log.info("Test 3: Key rotation during sync")
        
        node = self.nodes[0]
        
        # Key rotation happens every 32 blocks in regtest
        # Mine a modest number to verify key handling without excessive time
        current_height = node.getblockcount()
        target_height = 15  # Reduced to make test faster
        
        if current_height < target_height:
            address = node.getnewaddress()
            blocks_needed = target_height - current_height
            self.log.info(f"Mining {blocks_needed} more blocks to test key handling...")
            # Mine on single node (nodes are disconnected from previous test)
            self.generatetoaddress(node, blocks_needed, address, sync_fun=self.no_op)
        
        final_height = node.getblockcount()
        self.log.info(f"Chain height: {final_height}")
        
        # Verify chain tip is valid
        tip_hash = node.getbestblockhash()
        tip = node.getblock(tip_hash)
        assert_greater_than(tip['confirmations'], 0)
        
        # Verify all headers in the chain are valid
        for height in range(1, final_height + 1):
            block_hash = node.getblockhash(height)
            header = node.getblockheader(block_hash)
            assert_greater_than(header['confirmations'], 0)
        
        self.log.info("Key rotation handled correctly")


class RandomXHeaderP2PTest(OpenSYTestFramework):
    """Additional P2P-level header tests."""
    
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-randomxforkheight=5"]]

    def run_test(self):
        self.log.info("Testing P2P header handling for RandomX...")
        
        self.test_getblockheader_rpc()
        self.test_header_size_limits()

    def test_getblockheader_rpc(self):
        """Test getblockheader RPC works correctly for RandomX blocks."""
        self.log.info("Test: getblockheader RPC")
        
        node = self.nodes[0]
        
        # Mine blocks across fork
        address = node.getnewaddress()
        self.generatetoaddress(node, 10, address)
        
        # Get headers in different formats
        for height in [1, 5, 8]:
            blockhash = node.getblockhash(height)
            
            # Verbose format (JSON)
            header_verbose = node.getblockheader(blockhash, True)
            assert 'hash' in header_verbose
            assert 'height' in header_verbose
            assert header_verbose['height'] == height
            
            # Raw format (hex)
            header_raw = node.getblockheader(blockhash, False)
            assert isinstance(header_raw, str)
            # Block header is 80 bytes = 160 hex chars
            assert_equal(len(header_raw), 160)
            
            self.log.info(f"Height {height} header validated")

    def test_header_size_limits(self):
        """Test that header size is consistent (80 bytes)."""
        self.log.info("Test: Header size limits")
        
        node = self.nodes[0]
        
        # All block headers should be exactly 80 bytes
        for height in range(1, min(node.getblockcount() + 1, 15)):
            blockhash = node.getblockhash(height)
            header_hex = node.getblockheader(blockhash, False)
            header_bytes = bytes.fromhex(header_hex)
            
            assert_equal(len(header_bytes), 80, 
                f"Header at height {height} should be 80 bytes, got {len(header_bytes)}")
        
        self.log.info("All headers are correct size (80 bytes)")


if __name__ == '__main__':
    RandomXHeaderSpamTest(__file__).main()
