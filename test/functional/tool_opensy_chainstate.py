#!/usr/bin/env python3
# Copyright (c) 2022-present The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import subprocess

from test_framework.test_framework import OpenSYTestFramework

class OpenSYChainstateTest(OpenSYTestFramework):
    def skip_test_if_missing_module(self):
        self.skip_if_no_opensy_chainstate()

    def set_test_params(self):
        self.setup_clean_chain = True
        self.chain = "regtest"  # Use regtest instead of mainnet
        self.num_nodes = 1

    def add_block(self, datadir, input, expected_stderr):
        proc = subprocess.Popen(
            self.get_binaries().chainstate_argv() + [datadir],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        stdout, stderr = proc.communicate(input=input + "\n", timeout=5)
        self.log.debug("STDOUT: {0}".format(stdout.strip("\n")))
        self.log.info("STDERR: {0}".format(stderr.strip("\n")))

        if expected_stderr not in stderr:
            raise AssertionError(f"Expected stderr output {expected_stderr} does not partially match stderr:\n{stderr}")

    def run_test(self):
        node = self.nodes[0]
        
        # Mine a block to get valid block data for this chain
        self.generate(node, 1)
        block_one_hash = node.getblockhash(1)
        block_one = node.getblock(block_one_hash, 0)  # Get raw hex
        
        datadir = node.cli.datadir
        node.stop_node()

        self.log.info(f"Testing opensy-chainstate {self.get_binaries().chainstate_argv()} with datadir: {datadir}")
        
        # Test with a valid block (should be accepted but already known)
        self.add_block(datadir, block_one, "duplicate")
        # Test with invalid hex
        self.add_block(datadir, "00", "Block decode failed")
        # Test with empty input
        self.add_block(datadir, "", "Empty line found")

if __name__ == "__main__":
    OpenSYChainstateTest(__file__).main()
