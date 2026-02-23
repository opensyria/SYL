#!/usr/bin/env python3
# Copyright (c) 2025-2026 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""SRC-20 token error recovery tests.

Verifies the node continues operating correctly after failed token operations:
- Operations succeed after failed issuance (fee override cleanup, R17-02)
- Operations succeed after failed transfer/burn
- Rapid fail/succeed cycles don't leak state
- Node mines and syncs normally after a storm of RPC errors
- Multi-node state stays consistent through error sequences
"""

from test_framework.test_framework import OpenSYTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
)


class TokenErrorRecoveryTest(OpenSYTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ["-randomxforkheight=5", "-maxtxfee=500"],
            ["-randomxforkheight=5", "-maxtxfee=500"],
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.node0 = self.nodes[0]
        self.node1 = self.nodes[1]

        self.log.info("Mining initial blocks...")
        self.addr0 = self.node0.getnewaddress()
        self.addr1 = self.node1.getnewaddress()
        self.generatetoaddress(self.node0, 120, self.addr0)
        self.sync_all()

        self.log.info("=== Recovery After Failed Issuance ===")
        self.test_recovery_after_failed_issuance()

        self.log.info("=== Recovery After Failed Transfer ===")
        self.test_recovery_after_failed_transfer()

        self.log.info("=== Recovery After Failed Burn ===")
        self.test_recovery_after_failed_burn()

        self.log.info("=== Rapid Fail/Succeed Cycles ===")
        self.test_rapid_fail_succeed_cycles()

        self.log.info("=== Node Operations After Error Storm ===")
        self.test_node_after_error_storm()

        self.log.info("All error recovery tests passed!")

    def test_recovery_after_failed_issuance(self):
        """After a failed issuance (reserved ticker), a valid issuance should work."""
        # Attempt reserved ticker — should fail
        self.log.info("  Test: Fail then succeed issuance")
        assert_raises_rpc_error(-8, None, self.node0.walletissuetoken,
                               "SYL", "Reserved", 0, 1000)

        # Attempt duplicate params validation error
        assert_raises_rpc_error(-8, None, self.node0.walletissuetoken,
                               "", "Empty Ticker", 0, 1000)

        # Now a valid issuance should work fine
        result = self.node0.walletissuetoken("REC1", "Recovery One", 8, 1000000)
        assert 'txid' in result
        token_id = result['token_id']
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        info = self.node0.gettokeninfo(token_id)
        assert_equal(info['ticker'], "REC1")
        assert_equal(info['total_supply'], 1000000)

    def test_recovery_after_failed_transfer(self):
        """After a failed transfer (insufficient balance), a valid transfer should work."""
        self.log.info("  Test: Fail then succeed transfer")
        result = self.node0.walletissuetoken("REC2", "Recovery Two", 0, 10000)
        token_id = result['token_id']
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        # Attempt to transfer more than balance
        assert_raises_rpc_error(-4, None, self.node0.wallettransfertoken,
                               token_id, self.addr1, 99999)

        # Valid transfer should work
        self.node0.wallettransfertoken(token_id, self.addr1, 5000)
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        # Verify balances on both nodes
        bal1 = self.node1.gettokenbalances()
        token_bal = [b for b in bal1 if b.get('token_id') == token_id]
        assert_equal(len(token_bal), 1)
        assert_equal(token_bal[0]['balance'], 5000)

    def test_recovery_after_failed_burn(self):
        """After a failed burn (insufficient balance), a valid burn should work."""
        self.log.info("  Test: Fail then succeed burn")
        result = self.node0.walletissuetoken("REC3", "Recovery Three", 0, 10000)
        token_id = result['token_id']
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        # Attempt to burn more than balance
        assert_raises_rpc_error(-4, None, self.node0.walletburntoken,
                               token_id, 99999)

        # Valid burn should work
        self.node0.walletburntoken(token_id, 3000)
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        info = self.node0.gettokeninfo(token_id)
        assert_equal(info['circulating_supply'], 7000)

    def test_rapid_fail_succeed_cycles(self):
        """Alternate between invalid and valid operations rapidly."""
        self.log.info("  Test: 10 rapid fail/succeed cycles")
        result = self.node0.walletissuetoken("CYCL", "Cycle Test", 0, 100000)
        token_id = result['token_id']
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        expected_balance = 100000
        for i in range(10):
            # Invalid: transfer too much
            assert_raises_rpc_error(-4, None, self.node0.wallettransfertoken,
                                   token_id, self.addr1, 999999)
            # Valid: transfer 100
            self.node0.wallettransfertoken(token_id, self.addr1, 100)
            expected_balance -= 100
            self.generatetoaddress(self.node0, 1, self.addr0)

        self.sync_all()

        # Verify final state
        info = self.node0.gettokeninfo(token_id)
        assert_equal(info['circulating_supply'], 100000)  # no burns happened

        # Verify sender balance
        bal0 = self.node0.gettokenbalances()
        token_bal0 = [b for b in bal0 if b.get('token_id') == token_id]
        if len(token_bal0) > 0:
            assert_equal(token_bal0[0]['balance'], expected_balance)

    def test_node_after_error_storm(self):
        """Fire many different error types, then verify node still works."""
        self.log.info("  Test: Error storm then verify node health")
        fake_id = "0" * 64

        # Fire 20+ different errors
        errors = [
            lambda: self.node0.walletissuetoken("", "Empty", 0, 100),
            lambda: self.node0.walletissuetoken("X", "Short", 0, 100),
            lambda: self.node0.walletissuetoken("ABCDEF", "Long", 0, 100),
            lambda: self.node0.walletissuetoken("low", "Low", 0, 100),
            lambda: self.node0.walletissuetoken("SYL", "Reserved", 0, 100),
            lambda: self.node0.walletissuetoken("ZERO", "Zero", 0, 0),
            lambda: self.node0.gettokeninfo("invalid"),
            lambda: self.node0.gettokeninfo(fake_id),
            lambda: self.node0.wallettransfertoken(fake_id, self.addr1, 100),
            lambda: self.node0.walletburntoken(fake_id, 100),
            lambda: self.node0.wallettransfertoken("invalid", self.addr1, 1),
            lambda: self.node0.walletburntoken("invalid", 1),
        ]

        error_count = 0
        for err_fn in errors:
            try:
                err_fn()
            except Exception:
                error_count += 1

        self.log.info(f"  Fired {error_count} expected errors")
        assert_greater_than(error_count, 10)

        # Node should still be healthy: mine a block
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        # Issue a new token to prove the node is fully functional
        result = self.node0.walletissuetoken("HLTH", "Health Check", 0, 999)
        assert 'txid' in result
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        info = self.node0.gettokeninfo(result['token_id'])
        assert_equal(info['ticker'], "HLTH")
        assert_equal(info['total_supply'], 999)

        # Both nodes agree
        info1 = self.node1.gettokeninfo(result['token_id'])
        assert_equal(info1['ticker'], "HLTH")
        assert_equal(info1['total_supply'], 999)


if __name__ == '__main__':
    TokenErrorRecoveryTest(__file__).main()
