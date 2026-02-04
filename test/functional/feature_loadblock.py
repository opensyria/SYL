#!/usr/bin/env python3
# Copyright (c) 2017-2021 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test loadblock option

Test the option to start a node with the option loadblock which loads
a serialized blockchain from a file (usually called bootstrap.dat).
"""

from pathlib import Path
import struct

from test_framework.blocktools import COINBASE_MATURITY
from test_framework.test_framework import OpenSYTestFramework
from test_framework.util import assert_equal


class LoadblockTest(OpenSYTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.supports_cli = False

    def run_test(self):
        self.nodes[1].setnetworkactive(state=False)
        self.generate(self.nodes[0], COINBASE_MATURITY, sync_fun=self.no_op)

        bootstrap_file = Path(self.options.tmpdir) / "bootstrap.dat"

        self.log.info("Create bootstrap.dat by fetching blocks via RPC")
        # Network magic for regtest: SYLR (0x53, 0x59, 0x4c, 0x52)
        netmagic = bytes([0x53, 0x59, 0x4c, 0x52])

        with open(bootstrap_file, "wb") as f:
            for height in range(101):  # 0 to 100 inclusive
                block_hash = self.nodes[0].getblockhash(height)
                block_data = bytes.fromhex(self.nodes[0].getblock(block_hash, 0))
                # Write: magic (4 bytes) + size (4 bytes LE) + block data
                f.write(netmagic)
                f.write(struct.pack("<I", len(block_data)))
                f.write(block_data)

        self.log.info("Restart second, unsynced node with bootstrap file")
        self.restart_node(1, extra_args=[f"-loadblock={bootstrap_file}"])
        assert_equal(self.nodes[1].getblockcount(), 100)  # start_node is blocking on all block files being imported

        assert_equal(self.nodes[1].getblockchaininfo()['blocks'], 100)
        assert_equal(self.nodes[0].getbestblockhash(), self.nodes[1].getbestblockhash())


if __name__ == '__main__':
    LoadblockTest(__file__).main()
