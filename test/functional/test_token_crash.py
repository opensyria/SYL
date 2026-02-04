#!/usr/bin/env python3
# Copyright (c) 2025 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Minimal test to reproduce token transfer crash."""

from test_framework.test_framework import OpenSYTestFramework

class TokenCrashTest(OpenSYTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-randomxforkheight=5", "-debug=token"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Mining blocks...")
        addr = self.nodes[0].getnewaddress()
        self.generatetoaddress(self.nodes[0], 110, addr)
        
        self.log.info("Issuing token...")
        result = self.nodes[0].walletissuetoken("TST1", "Test Token", 8, 1000000)
        token_id = result['token_id']
        self.generatetoaddress(self.nodes[0], 1, addr)
        
        addr1 = self.nodes[0].getnewaddress()
        
        self.log.info("First transfer...")
        self.nodes[0].wallettransfertoken(token_id, addr1, 10000)
        
        self.log.info("Sync queue...")
        self.nodes[0].syncwithvalidationinterfacequeue()
        
        self.log.info("Second transfer...")
        self.nodes[0].wallettransfertoken(token_id, addr1, 20000)
        
        self.log.info("SUCCESS - No crash!")

if __name__ == '__main__':
    TokenCrashTest(__file__).main()
