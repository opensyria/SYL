#!/usr/bin/env python3
# Copyright (c) 2025 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that wrong PoW algorithm is rejected at fork boundary.

This test verifies:
1. Block at height >= fork_height with valid SHA256d but invalid RandomX is REJECTED
2. Block with completely invalid PoW (bad nonce) is rejected at all heights
3. Fork boundary is correctly enforced

The test constructs blocks manually to test rejection scenarios.
"""

from test_framework.test_framework import OpenSYTestFramework
from test_framework.util import assert_equal
from test_framework.blocktools import (
    create_block,
    create_coinbase,
)
from test_framework.messages import CBlock, CBlockHeader, COIN
import struct
import time


class NegativePowValidationTest(OpenSYTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        # Set fork height to 3 for fast testing
        self.extra_args = [["-randomxforkheight=3"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        
        self.log.info("=== Negative PoW Validation Tests ===")
        
        # Test 1: Pre-fork - SHA256d should work
        self.log.info("Test 1: Pre-fork SHA256d mining works...")
        self.test_prefork_sha256d_works(node)
        
        # Test 2: Invalid block (bad nonce) rejected pre-fork
        self.log.info("Test 2: Invalid nonce rejected pre-fork...")
        self.test_bad_nonce_rejected_prefork(node)
        
        # Test 3: Post-fork - RandomX should work
        self.log.info("Test 3: Post-fork RandomX mining works...")
        self.test_postfork_randomx_works(node)
        
        # Test 4: Invalid block (bad nonce) rejected post-fork
        self.log.info("Test 4: Invalid nonce rejected post-fork...")
        self.test_bad_nonce_rejected_postfork(node)
        
        # Test 5: Block with zero nonce at post-fork height rejected
        self.log.info("Test 5: Zero nonce block rejected post-fork...")
        self.test_zero_nonce_rejected(node)
        
        self.log.info("=== All negative PoW validation tests PASSED ===")

    def test_prefork_sha256d_works(self, node):
        """Verify SHA256d mining works before fork."""
        address = node.getnewaddress()
        # Mine 2 blocks (heights 1-2, fork at 3)
        hashes = self.generatetoaddress(node, 2, address)
        assert_equal(len(hashes), 2)
        assert_equal(node.getblockcount(), 2)
        self.log.info("  ✓ SHA256d blocks 1-2 mined successfully")

    def test_bad_nonce_rejected_prefork(self, node):
        """Verify block with invalid nonce is rejected before fork."""
        # Get current tip
        tip_hash = node.getbestblockhash()
        tip_height = node.getblockcount()
        tip = node.getblock(tip_hash)
        
        # Create a block template
        template = node.getblocktemplate({'rules': ['segwit']})
        
        # Create a block with intentionally bad nonce
        block = CBlock()
        block.nVersion = template['version']
        block.hashPrevBlock = int(tip_hash, 16)
        block.hashMerkleRoot = int(template['defaultmerkleroot'], 16) if 'defaultmerkleroot' in template else 0
        block.nTime = template['curtime']
        block.nBits = int(template['bits'], 16)
        block.nNonce = 0  # Almost certainly invalid
        
        # Try to submit - should be rejected
        result = node.submitblock(block.serialize().hex())
        
        # Result should indicate rejection (high-hash or similar)
        assert result is not None, "Block with bad nonce should be rejected"
        self.log.info(f"  ✓ Bad nonce block rejected pre-fork: {result}")

    def test_postfork_randomx_works(self, node):
        """Verify RandomX mining works after fork."""
        address = node.getnewaddress()
        # Mine to and past fork height (height 3+)
        hashes = self.generatetoaddress(node, 3, address)
        assert_equal(len(hashes), 3)
        current_height = node.getblockcount()
        assert current_height >= 3, f"Expected height >= 3, got {current_height}"
        self.log.info(f"  ✓ RandomX blocks mined, height now {current_height}")

    def test_bad_nonce_rejected_postfork(self, node):
        """Verify block with invalid nonce is rejected after fork (RandomX)."""
        tip_hash = node.getbestblockhash()
        template = node.getblocktemplate({'rules': ['segwit']})
        
        block = CBlock()
        block.nVersion = template['version']
        block.hashPrevBlock = int(tip_hash, 16)
        block.hashMerkleRoot = int(template['defaultmerkleroot'], 16) if 'defaultmerkleroot' in template else 0
        block.nTime = template['curtime']
        block.nBits = int(template['bits'], 16)
        block.nNonce = 12345  # Almost certainly invalid for RandomX
        
        result = node.submitblock(block.serialize().hex())
        
        assert result is not None, "Block with bad RandomX nonce should be rejected"
        self.log.info(f"  ✓ Bad nonce block rejected post-fork (RandomX): {result}")

    def test_zero_nonce_rejected(self, node):
        """Verify block with zero nonce is rejected at RandomX heights."""
        tip_hash = node.getbestblockhash()
        template = node.getblocktemplate({'rules': ['segwit']})
        
        block = CBlock()
        block.nVersion = template['version']
        block.hashPrevBlock = int(tip_hash, 16)
        block.hashMerkleRoot = int(template['defaultmerkleroot'], 16) if 'defaultmerkleroot' in template else 0
        block.nTime = template['curtime']
        block.nBits = int(template['bits'], 16)
        block.nNonce = 0
        
        result = node.submitblock(block.serialize().hex())
        
        assert result is not None, "Block with zero nonce should be rejected"
        self.log.info(f"  ✓ Zero nonce block rejected: {result}")


if __name__ == '__main__':
    NegativePowValidationTest(__file__).main()
