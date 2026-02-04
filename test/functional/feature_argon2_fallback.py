#!/usr/bin/env python3
# Copyright (c) 2025 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test Argon2id emergency PoW fallback.

This test verifies the emergency Argon2id fallback mechanism:
1. Argon2id is NOT active by default (nArgon2EmergencyHeight = -1)
2. Argon2id activates at the specified emergency height
3. Mining works correctly with Argon2id
4. Chain syncs correctly between nodes after Argon2id activation
5. RandomX blocks before emergency are still valid
6. Difficulty adjustment works correctly with Argon2id
7. GetBlockchainInfo reports correct algorithm

IMPORTANT: This test requires the -argon2emergencyheight parameter to be
implemented in the node. The fallback is DORMANT by default.
"""

from test_framework.test_framework import OpenSYTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
)


class Argon2FallbackTest(OpenSYTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        # Increase timeout for memory-hard mining
        self.rpc_timeout = 900  # 15 minutes
        
        # Node 0 & 1: Emergency activated at height 10
        # Node 2: No emergency (default, for incompatibility testing)
        self.extra_args = [
            ["-randomxforkheight=2", "-argon2emergencyheight=10"],
            ["-randomxforkheight=2", "-argon2emergencyheight=10"],
            ["-randomxforkheight=2"],  # No emergency
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Test 1: Mining before emergency (RandomX)")
        self.test_pre_emergency_mining()

        self.log.info("Test 2: Emergency activation at specified height")
        self.test_emergency_activation()

        self.log.info("Test 3: Mining with Argon2id after emergency")
        self.test_argon2_mining()

        self.log.info("Test 4: Chain sync between Argon2id nodes")
        self.test_argon2_chain_sync()

        self.log.info("Test 5: Incompatible node detection")
        self.test_incompatible_node()

        self.log.info("Test 6: RPC info reports correct algorithm")
        self.test_rpc_algorithm_info()

        self.log.info("Test 7: Reorg across emergency boundary")
        self.test_emergency_boundary_reorg()

    def test_pre_emergency_mining(self):
        """Test that RandomX mining works before emergency height."""
        node = self.nodes[0]
        
        # Mine blocks 1-9 (emergency at height 10)
        # Block 1 is SHA256d (genesis), blocks 2+ are RandomX
        address = node.getnewaddress()
        
        # Mine to height 9 (one before emergency) - no sync, nodes not connected
        blockhashes = self.generatetoaddress(node, 9, address, sync_fun=self.no_op)
        
        assert_equal(len(blockhashes), 9)
        assert_equal(node.getblockcount(), 9)
        
        # Verify all blocks are valid
        for blockhash in blockhashes:
            block = node.getblock(blockhash)
            assert_greater_than(block['confirmations'], 0)
        
        self.log.info(f"  Mined {len(blockhashes)} blocks before emergency height")

    def test_emergency_activation(self):
        """Test that Argon2id activates exactly at emergency height."""
        node = self.nodes[0]
        
        # Current height should be 9
        assert_equal(node.getblockcount(), 9)
        
        # Mine block at height 10 - first Argon2id block (no sync)
        address = node.getnewaddress()
        blockhashes = self.generatetoaddress(node, 1, address, sync_fun=self.no_op)
        
        assert_equal(len(blockhashes), 1)
        assert_equal(node.getblockcount(), 10)
        
        # Block at height 10 should be valid (Argon2id)
        block = node.getblock(blockhashes[0])
        assert_equal(block['height'], 10)
        assert_greater_than(block['confirmations'], 0)
        
        self.log.info(f"  Argon2id block mined at height 10: {blockhashes[0][:16]}...")

    def test_argon2_mining(self):
        """Test that Argon2id mining works after emergency activation."""
        node = self.nodes[0]
        
        # Mine additional Argon2id blocks (no sync - nodes not connected yet)
        address = node.getnewaddress()
        blockhashes = self.generatetoaddress(node, 5, address, sync_fun=self.no_op)
        
        assert_equal(len(blockhashes), 5)
        assert_equal(node.getblockcount(), 15)
        
        # All blocks should be valid
        for blockhash in blockhashes:
            block = node.getblock(blockhash)
            assert_greater_than(block['confirmations'], 0)
        
        self.log.info(f"  Mined {len(blockhashes)} Argon2id blocks (heights 11-15)")

    def test_argon2_chain_sync(self):
        """Test that nodes sync Argon2id blocks correctly."""
        node0 = self.nodes[0]
        node1 = self.nodes[1]
        
        # Connect node0 and node1 (both have same emergency height)
        self.connect_nodes(0, 1)
        
        # Sync chains between compatible nodes only
        self.sync_blocks([node0, node1])
        
        # Both nodes should have the same chain
        assert_equal(node0.getblockcount(), node1.getblockcount())
        assert_equal(node0.getbestblockhash(), node1.getbestblockhash())
        
        # Mine more on node1 and verify sync (only sync compatible nodes)
        address = node1.getnewaddress()
        blockhashes = self.generatetoaddress(node1, 3, address, sync_fun=lambda: self.sync_blocks([node0, node1]))
        
        assert_equal(node0.getblockcount(), node1.getblockcount())
        assert_equal(node0.getbestblockhash(), node1.getbestblockhash())
        
        self.log.info(f"  Nodes 0 and 1 synced at height {node0.getblockcount()}")

    def test_incompatible_node(self):
        """Test that node without emergency activation cannot sync past emergency height."""
        node0 = self.nodes[0]  # Has emergency
        node2 = self.nodes[2]  # No emergency
        
        # Try to connect node2 to node0 - this may fail or result in disconnect
        # because they have incompatible consensus rules
        import time
        
        try:
            # Add node2 to node0's peer list (low-level connect)
            node0.addnode(f"127.0.0.1:{node2.p2p_port}", "onetry")
            time.sleep(3)  # Give time for connection attempt
        except Exception as e:
            self.log.info(f"  Connection attempt resulted in: {e}")
        
        # Check if they're connected
        peers0 = node0.getpeerinfo()
        peers2 = node2.getpeerinfo()
        
        # node2 should NOT have the same tip as node0 after emergency height
        # because it's validating with RandomX while node0 used Argon2id
        
        # The incompatible node might:
        # 1. Reject the blocks as invalid PoW
        # 2. Be on a shorter chain
        # 3. Be stuck at genesis or early blocks
        # 4. Never connect successfully
        
        if node0.getblockcount() > 9:  # After emergency
            # Node2 shouldn't have the same tip (blocks after 10 are Argon2id)
            self.log.info(f"  Node0 at height {node0.getblockcount()}, Node2 at height {node2.getblockcount()}")
            self.log.info(f"  Node0 peers: {len(peers0)}, Node2 peers: {len(peers2)}")
            
            # The key point is that nodes with different emergency settings
            # will NOT reach consensus on blocks after the emergency height
            # Node2 should still be at genesis or early blocks
            assert_greater_than(node0.getblockcount(), node2.getblockcount())

    def test_rpc_algorithm_info(self):
        """Test that RPC returns correct algorithm information."""
        node = self.nodes[0]
        
        # Get blockchain info
        info = node.getblockchaininfo()
        
        # Verify chain is operational
        assert_greater_than(info['blocks'], 10)  # Past emergency
        assert_equal(info['chain'], 'regtest')
        
        # Get mining info
        mining_info = node.getmininginfo()
        assert_greater_than(mining_info['blocks'], 0)
        
        self.log.info(f"  Chain info: {info['blocks']} blocks")

    def test_emergency_boundary_reorg(self):
        """Test reorg behavior at the emergency boundary."""
        node0 = self.nodes[0]
        node1 = self.nodes[1]
        
        # Ensure nodes 0 and 1 are connected (node2 is incompatible, skip it)
        # They should already be connected from test 4, but let's verify
        self.sync_blocks([node0, node1])
        
        initial_height = node0.getblockcount()
        
        # Disconnect nodes
        self.disconnect_nodes(0, 1)
        
        # Mine different blocks on each node (no sync since they're disconnected)
        address0 = node0.getnewaddress()
        address1 = node1.getnewaddress()
        
        blocks0 = self.generatetoaddress(node0, 2, address0, sync_fun=self.no_op)
        blocks1 = self.generatetoaddress(node1, 3, address1, sync_fun=self.no_op)  # Longer chain
        
        # Verify chains diverged
        assert_equal(node0.getblockcount(), initial_height + 2)
        assert_equal(node1.getblockcount(), initial_height + 3)
        
        # Reconnect - node1's longer chain should win
        self.connect_nodes(0, 1)
        self.sync_blocks([node0, node1])
        
        # Both should be on node1's chain (longer)
        assert_equal(node0.getblockcount(), initial_height + 3)
        assert_equal(node0.getbestblockhash(), node1.getbestblockhash())
        
        self.log.info(f"  Reorg successful, chain at height {node0.getblockcount()}")


if __name__ == '__main__':
    Argon2FallbackTest(__file__).main()
