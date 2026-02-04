#!/usr/bin/env python3
# Copyright (c) 2024 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Generate assumeutxo data from deterministic test chain.

This script generates the assumeutxo chainparams data for regtest.
It replicates the exact block generation of feature_assumeutxo.py.
"""

from test_framework.test_framework import OpenSYTestFramework
from test_framework.util import assert_equal
from test_framework.wallet import MiniWallet

START_HEIGHT = 199
SNAPSHOT_BASE_HEIGHT = 299


class GetAssumeutxoHashes(OpenSYTestFramework):

    def set_test_params(self):
        """Use the pregenerated, deterministic chain up to height 199."""
        self.num_nodes = 1
        self.rpc_timeout = 120
        self.extra_args = [["-coinstatsindex=1"]]

    def setup_network(self):
        """Start like feature_assumeutxo - don't generate IBD-prevention block."""
        self.add_nodes(1)
        self.start_nodes(extra_args=self.extra_args)

    def run_test(self):
        node = self.nodes[0]
        
        # Create MiniWallet same as feature_assumeutxo
        self.mini_wallet = MiniWallet(node)

        # Mock time for deterministic chain (same as feature_assumeutxo.py)
        node.setmocktime(node.getblockheader(node.getbestblockhash())['time'])

        # Verify we start at the expected height
        start_height = node.getblockcount()
        self.log.info(f"Starting at height {start_height}")
        
        if start_height != START_HEIGHT:
            self.log.warning(f"Expected start height {START_HEIGHT}, got {start_height}")
            self.log.warning("The cache may be out of sync. Delete the cache and try again.")

        # Generate blocks up to SNAPSHOT_BASE_HEIGHT, same as feature_assumeutxo.py
        # This includes MiniWallet transactions every 3 blocks and stale block at i==4
        blocks_needed = SNAPSHOT_BASE_HEIGHT - start_height
        self.log.info(f"Generating {blocks_needed} blocks to reach height {SNAPSHOT_BASE_HEIGHT}")
        
        for i in range(blocks_needed):
            if i % 3 == 0:
                self.mini_wallet.send_self_transfer(from_node=node)
            self.generate(node, nblocks=1, sync_fun=self.no_op)
            if i == 4:
                # Create a stale block that forks off the main chain (same as feature_assumeutxo.py)
                temp_invalid = node.getbestblockhash()
                node.invalidateblock(temp_invalid)
                stale_hash = self.generateblock(node, output="raw(aaaa)", transactions=[], sync_fun=self.no_op)["hash"]
                node.invalidateblock(stale_hash)
                node.reconsiderblock(temp_invalid)

        assert_equal(node.getblockcount(), SNAPSHOT_BASE_HEIGHT)

        # Wait for coinstats index
        self.wait_until(lambda: node.getindexinfo()["coinstatsindex"]["synced"])

        # For heights 110 and 200, we can only get muhash (which won't work for validation)
        # For height 299, we need hash_serialized_3 which is only available at the tip
        
        # First, get the values at heights 110 and 200 using muhash
        # Note: These won't be used for validation - only height 299 matters for the test
        heights = [110, 200]
        
        self.log.info("\n\n=== ASSUMEUTXO DATA (muhash - informational only) ===\n")
        
        for height in heights:
            blockhash = node.getblockhash(height)
            coinstats = node.gettxoutsetinfo(hash_type="muhash", hash_or_height=height, use_index=True)
            
            self.log.info(f"Height {height}:")
            self.log.info(f"  muhash = \"{coinstats['muhash']}\"")
            self.log.info(f"  blockhash = \"{blockhash}\"")
            self.log.info(f"  txouts = {coinstats['txouts']}")
            self.log.info("")
        
        # For height 299 (the current tip), get hash_serialized_3
        self.log.info("\n=== HEIGHT 299 (at tip - using hash_serialized_3) ===\n")
        
        assert_equal(node.getblockcount(), 299)
        blockhash = node.getblockhash(299)
        coinstats = node.gettxoutsetinfo(hash_type="hash_serialized_3")  # At current tip
        
        self.log.info(f"Height 299:")
        self.log.info(f"  hash_serialized_3 = \"{coinstats['hash_serialized_3']}\"")
        self.log.info(f"  blockhash = \"{blockhash}\"")
        self.log.info(f"  txouts = {coinstats['txouts']}")
        
        self.log.info("\n=== C++ FORMAT FOR chainparams.cpp (height 299) ===\n")
        
        print(f"""        {{
            // For use by test/functional/feature_assumeutxo.py
            .height = 299,
            .hash_serialized = AssumeutxoHash{{uint256{{"{coinstats['hash_serialized_3']}"}}}},
            .m_chain_tx_count = {coinstats['txouts']},
            .blockhash = uint256{{"{blockhash}"}},
        }},""")
        
        # Also generate txoutset_hash for feature_assumeutxo.py
        self.log.info("\n\n=== TXOUTSET HASHES FOR feature_assumeutxo.py ===\n")
        
        # Dump at height 299
        dump_299 = node.dumptxoutset('utxos299.dat', "latest")
        self.log.info(f"Height 299 txoutset_hash: {dump_299['txoutset_hash']}")
        
        # For height 298, we need to rollback
        dump_298 = node.dumptxoutset('utxos298.dat', rollback=298)
        self.log.info(f"Height 298 txoutset_hash: {dump_298['txoutset_hash']}")


if __name__ == '__main__':
    GetAssumeutxoHashes(__file__).main()
