#!/usr/bin/env python3
# Copyright (c) 2024 The OpenSY developers
# Generate the corrupted snapshot hashes for feature_assumeutxo.py

from test_framework.test_framework import OpenSYTestFramework
from test_framework.wallet import MiniWallet
from test_framework.util import assert_equal
from test_framework.compressor import compress_amount
from test_framework.messages import MAX_MONEY, ser_varint
import hashlib

START_HEIGHT = 199
SNAPSHOT_BASE_HEIGHT = 299


class GenCorruptedHashes(OpenSYTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.rpc_timeout = 120
        self.extra_args = [["-coinstatsindex=1"]]

    def setup_network(self):
        self.add_nodes(1)
        self.start_nodes(extra_args=self.extra_args)

    def run_test(self):
        node = self.nodes[0]
        self.mini_wallet = MiniWallet(node)
        node.setmocktime(node.getblockheader(node.getbestblockhash())['time'])
        
        start_height = node.getblockcount()
        blocks_needed = SNAPSHOT_BASE_HEIGHT - start_height
        
        for i in range(blocks_needed):
            if i % 3 == 0:
                self.mini_wallet.send_self_transfer(from_node=node)
            self.generate(node, nblocks=1, sync_fun=self.no_op)

        assert_equal(node.getblockcount(), SNAPSHOT_BASE_HEIGHT)
        
        # Create a snapshot
        dump_output = node.dumptxoutset('utxos.dat', "latest")
        snapshot_path = dump_output['path']
        
        with open(snapshot_path, 'rb') as f:
            valid_snapshot_contents = f.read()
        
        self.log.info(f"Snapshot size: {len(valid_snapshot_contents)} bytes")
        self.log.info(f"Original txoutset_hash: {dump_output['txoutset_hash']}")
        
        # The cases from feature_assumeutxo.py
        cases = [
            (b"\xff" * 32, 0, "wrong outpoint hash"),
            (b"\x01", 33, "wrong outpoint index"),
            (b"\x82", 34, "wrong coin code VARINT"),
            (b"\x80", 34, "another wrong coin code"),
        ]
        
        self.log.info("\n=== CORRUPTED HASHES ===\n")
        
        for content, offset, description in cases:
            # Create corrupted file
            corrupted = bytearray(valid_snapshot_contents)
            start_pos = 5 + 2 + 4 + 32 + 8 + offset
            for i, b in enumerate(content):
                corrupted[start_pos + i] = b
            
            # Compute the hash that would result
            # The hash is computed from the corrupted coin data
            # We compute a simple hash of the corrupted content for identification
            coin_data_start = 5 + 2 + 4 + 32 + 8  # after header
            coin_data = bytes(corrupted[coin_data_start:])
            
            # Actually let's try loading the corrupted file and see what hash is reported
            bad_path = snapshot_path + '.mod'
            with open(bad_path, 'wb') as f:
                f.write(corrupted)
            
            try:
                node.loadtxoutset(bad_path)
            except Exception as e:
                error_msg = str(e)
                self.log.info(f"{description}:")
                self.log.info(f"  Error: {error_msg[:200]}")
                # Try to extract the "got" hash from the error message
                if "got " in error_msg:
                    got_start = error_msg.find("got ") + 4
                    got_hash = error_msg[got_start:got_start+64]
                    self.log.info(f"  Wrong hash: {got_hash}")


if __name__ == '__main__':
    GenCorruptedHashes(__file__).main()
