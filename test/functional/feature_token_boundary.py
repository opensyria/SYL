#!/usr/bin/env python3
# Copyright (c) 2025-2026 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""SRC-20 token boundary condition tests.

Tests extreme values and edge cases that exercise fixed audit findings:
- INT64_MAX supply issuance and transfer (R18-04, R28-02)
- Supply overflow rejection (> INT64_MAX)
- Full balance transfer leaving zero
- Complete supply burn then further operation rejection
- 2-char and 5-char ticker rejection
- Holder count accuracy after burns to zero
- Error code precision for all rejection paths
"""

from test_framework.test_framework import OpenSYTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
)

INT64_MAX = 2**63 - 1


class TokenBoundaryTest(OpenSYTestFramework):
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

        self.log.info("=== Ticker Length Boundary Tests ===")
        self.test_ticker_length_boundaries()

        self.log.info("=== Supply Boundary Tests ===")
        self.test_supply_boundaries()

        self.log.info("=== Full Balance Transfer Tests ===")
        self.test_full_balance_transfer()

        self.log.info("=== Complete Burn Tests ===")
        self.test_complete_burn()

        self.log.info("=== Error Code Precision Tests ===")
        self.test_error_code_precision()

        self.log.info("All boundary tests passed!")

    def test_ticker_length_boundaries(self):
        """Verify ticker length constraints: min=3, max=4."""
        # 2-char ticker should be rejected
        self.log.info("  Test: 2-char ticker rejected")
        assert_raises_rpc_error(-8, None, self.node0.walletissuetoken,
                               "AB", "Two Char", 8, 1000000)

        # 1-char ticker should be rejected
        self.log.info("  Test: 1-char ticker rejected")
        assert_raises_rpc_error(-8, None, self.node0.walletissuetoken,
                               "A", "One Char", 8, 1000000)

        # 5-char ticker should be rejected
        self.log.info("  Test: 5-char ticker rejected")
        assert_raises_rpc_error(-8, None, self.node0.walletissuetoken,
                               "ABCDE", "Five Char", 8, 1000000)

        # 3-char ticker should succeed
        self.log.info("  Test: 3-char ticker accepted")
        result = self.node0.walletissuetoken("TST", "Three Char Test", 8, 1000000)
        assert 'txid' in result
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        # 4-char ticker should succeed
        self.log.info("  Test: 4-char ticker accepted")
        result = self.node0.walletissuetoken("FOUR", "Four Char Test", 8, 1000000)
        assert 'txid' in result
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

    def test_supply_boundaries(self):
        """Verify INT64_MAX supply cap enforcement (R18-04, R28-02)."""
        # INT64_MAX supply with decimals=0 should succeed
        self.log.info("  Test: INT64_MAX supply accepted (decimals=0)")
        result = self.node0.walletissuetoken("MAXI", "Max Supply", 0, INT64_MAX)
        assert 'txid' in result
        token_id = result['token_id']
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        info = self.node0.gettokeninfo(token_id)
        assert_equal(info['total_supply'], INT64_MAX)
        assert_equal(info['circulating_supply'], INT64_MAX)

        # INT64_MAX + 1 should be rejected
        self.log.info("  Test: INT64_MAX + 1 supply rejected")
        assert_raises_rpc_error(-8, None, self.node0.walletissuetoken,
                               "OVER", "Over Max", 0, INT64_MAX + 1)

        # Zero supply should be rejected
        self.log.info("  Test: Zero supply rejected")
        assert_raises_rpc_error(-8, None, self.node0.walletissuetoken,
                               "ZERO", "Zero Supply", 0, 0)

        # Decimals=19 should be rejected (max is 18)
        self.log.info("  Test: Decimals 19 rejected")
        assert_raises_rpc_error(-8, None, self.node0.walletissuetoken,
                               "DEC", "Bad Decimals", 19, 1000)

        # High supply + high decimals causing display overflow should be rejected
        self.log.info("  Test: Supply * 10^decimals overflow rejected")
        # With decimals=18, max safe supply is UINT64_MAX / 10^18 ≈ 18.44
        # Supply of 100 with decimals=18 → 100 * 10^18 > UINT64_MAX → rejected
        assert_raises_rpc_error(-8, None, self.node0.walletissuetoken,
                               "DISP", "Display Overflow", 18, 100)

    def test_full_balance_transfer(self):
        """Transfer exact full balance, leaving sender with zero."""
        self.log.info("  Test: Issue and transfer full balance")
        result = self.node0.walletissuetoken("FULL", "Full Transfer", 0, 1000000)
        token_id = result['token_id']
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        # Transfer entire supply
        self.node0.wallettransfertoken(token_id, self.addr1, 1000000)
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        # Sender should now have zero balance (or token not listed)
        balances0 = self.node0.gettokenbalances()
        token_bal = [b for b in balances0 if b.get('token_id') == token_id]
        if len(token_bal) > 0:
            assert_equal(token_bal[0]['balance'], 0)

        # Receiver should have full balance
        balances1 = self.node1.gettokenbalances()
        token_bal1 = [b for b in balances1 if b.get('token_id') == token_id]
        assert_equal(len(token_bal1), 1)
        assert_equal(token_bal1[0]['balance'], 1000000)

        # Attempting another transfer of 1 from sender should fail with -4
        self.log.info("  Test: Transfer after zero balance fails with correct error")
        assert_raises_rpc_error(-4, None, self.node0.wallettransfertoken,
                               token_id, self.addr1, 1)

        # Token info should show 1 holder (not 2)
        info = self.node0.gettokeninfo(token_id)
        assert_equal(info['holder_count'], 1)

    def test_complete_burn(self):
        """Burn entire circulating supply, then verify state."""
        self.log.info("  Test: Issue and burn entire supply")
        result = self.node0.walletissuetoken("BURN", "Burn Test", 0, 500000)
        token_id = result['token_id']
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        # Burn everything
        self.node0.walletburntoken(token_id, 500000)
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        # Token should still exist in the DB
        info = self.node0.gettokeninfo(token_id)
        assert_equal(info['total_supply'], 500000)
        assert_equal(info['circulating_supply'], 0)
        assert_equal(info['holder_count'], 0)

        # gettokenholders should return empty
        holders = self.node0.gettokenholders(token_id)
        assert_equal(len(holders), 0)

        # Further burn should fail
        self.log.info("  Test: Burn after zero balance fails")
        assert_raises_rpc_error(-4, None, self.node0.walletburntoken,
                               token_id, 1)

        # Further transfer should fail
        self.log.info("  Test: Transfer after zero balance fails")
        assert_raises_rpc_error(-4, None, self.node0.wallettransfertoken,
                               token_id, self.addr1, 1)

        # Node1 should see the same state
        info1 = self.node1.gettokeninfo(token_id)
        assert_equal(info1['circulating_supply'], 0)
        assert_equal(info1['holder_count'], 0)

    def test_error_code_precision(self):
        """Verify exact RPC error codes for all rejection paths."""
        # First create a valid token for transfer/burn error tests
        result = self.node0.walletissuetoken("ERRT", "Error Test", 0, 1000)
        token_id = result['token_id']
        self.generatetoaddress(self.node0, 1, self.addr0)
        self.sync_all()

        fake_id = "0" * 64

        # --- walletissuetoken error codes ---
        self.log.info("  Test: walletissuetoken error codes")
        # Invalid ticker → -8
        assert_raises_rpc_error(-8, None, self.node0.walletissuetoken,
                               "", "Name", 8, 1000)
        assert_raises_rpc_error(-8, None, self.node0.walletissuetoken,
                               "toolow", "Name", 8, 1000)
        # Duplicate ticker → -8
        assert_raises_rpc_error(-8, None, self.node0.walletissuetoken,
                               "ERRT", "Duplicate", 0, 1000)
        # Reserved ticker → -8
        assert_raises_rpc_error(-8, None, self.node0.walletissuetoken,
                               "SYL", "Reserved", 0, 1000)

        # --- wallettransfertoken error codes ---
        self.log.info("  Test: wallettransfertoken error codes")
        # Invalid token ID → -8
        assert_raises_rpc_error(-8, None, self.node0.wallettransfertoken,
                               "invalid", self.addr1, 100)
        # Non-existent token → -5
        assert_raises_rpc_error(-5, None, self.node0.wallettransfertoken,
                               fake_id, self.addr1, 100)
        # Invalid address → -5
        assert_raises_rpc_error(-5, None, self.node0.wallettransfertoken,
                               token_id, "invalid_address", 100)
        # Zero amount → -8
        assert_raises_rpc_error(-8, None, self.node0.wallettransfertoken,
                               token_id, self.addr1, 0)
        # Exceeds balance → -4
        assert_raises_rpc_error(-4, None, self.node0.wallettransfertoken,
                               token_id, self.addr1, 999999)

        # --- walletburntoken error codes ---
        self.log.info("  Test: walletburntoken error codes")
        # Invalid token ID → -8
        assert_raises_rpc_error(-8, None, self.node0.walletburntoken,
                               "invalid", 100)
        # Non-existent token → -5
        assert_raises_rpc_error(-5, None, self.node0.walletburntoken,
                               fake_id, 100)
        # Zero amount → -8
        assert_raises_rpc_error(-8, None, self.node0.walletburntoken,
                               token_id, 0)
        # Exceeds balance → -4
        assert_raises_rpc_error(-4, None, self.node0.walletburntoken,
                               token_id, 999999)

        # --- gettokeninfo error codes ---
        self.log.info("  Test: gettokeninfo error codes")
        assert_raises_rpc_error(-8, None, self.node0.gettokeninfo, "invalid")
        assert_raises_rpc_error(-5, None, self.node0.gettokeninfo, fake_id)


if __name__ == '__main__':
    TokenBoundaryTest(__file__).main()
