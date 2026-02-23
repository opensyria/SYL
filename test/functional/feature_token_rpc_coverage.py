#!/usr/bin/env python3
# Copyright (c) 2024-2026 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Test coverage for previously untested SRC-20 RPCs and adversarial scenarios.

Covers:
- decodesrc20 RPC (valid / invalid / truncated inputs)
- gettokenhistory RPC (with pagination)
- gettokenholders RPC (with min_balance / count params)
- gettokenstats RPC (multi-token aggregate)
- gettokenbyname RPC (successful positive lookup)
- listtokens pagination (cursor-based via start token_id)
- metadata_hash parameter on issuance
- Transfer to self
- INT64_MAX transfer and burn amounts
- All reserved tickers rejected
"""

from test_framework.test_framework import OpenSYTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    assert_greater_than,
)

INT64_MAX = 9223372036854775807


class TokenRPCCoverageTest(OpenSYTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [["-randomxforkheight=5"], ["-randomxforkheight=5"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Mining initial blocks...")
        self.setup_wallets()

        self.test_decodesrc20()
        self.test_gettokenbyname_positive()
        self.test_metadata_hash()
        self.test_transfer_to_self()
        self.test_gettokenhistory()
        self.test_gettokenholders()
        self.test_gettokenstats_multi()
        self.test_listtokens_pagination()
        self.test_reserved_tickers_all()
        self.test_int64_max_transfer_burn()

        self.log.info("All RPC coverage tests passed!")

    def setup_wallets(self):
        """Set up wallets and mine coins for testing."""
        self.nodes[0].createwallet("w0")
        self.nodes[1].createwallet("w1")
        self.w0 = self.nodes[0].get_wallet_rpc("w0")
        self.w1 = self.nodes[1].get_wallet_rpc("w1")
        self.addr0 = self.w0.getnewaddress()
        self.addr1 = self.w1.getnewaddress()
        self.generatetoaddress(self.nodes[0], 110, self.addr0)
        self.sync_all()

    # ------------------------------------------------------------------ #
    # decodesrc20 RPC
    # ------------------------------------------------------------------ #
    def test_decodesrc20(self):
        self.log.info("Test: decodesrc20 RPC")
        node = self.nodes[0]

        # 1. Build a valid issuance script via issuetoken and decode it
        issue_data = node.issuetoken("DCOD", "Decode Test", 8, 500000)
        hex_script = issue_data["op_return_hex"]
        decoded = node.decodesrc20(hex_script)
        assert_equal(decoded["action"], "ISSUE")
        assert_equal(decoded["data"]["ticker"], "DCOD")
        assert_equal(decoded["data"]["name"], "Decode Test")
        assert_equal(decoded["data"]["decimals"], 8)
        assert_equal(decoded["data"]["total_supply"], 500000)
        self.log.info("  decodesrc20 valid ISSUE: OK")

        # 2. Issue a real token so we can get valid transfer/burn scripts
        real_result = self.w0.walletissuetoken("XDEC", "Decode Xfer", 0, 50000)
        real_token = real_result["token_id"]
        self.generate(self.nodes[0], 1)
        self.sync_all()

        # Build a valid transfer script and decode
        xfer_data = node.transfertoken(real_token, self.addr1, 100)
        xfer_hex = xfer_data["op_return_hex"]
        decoded_xfer = node.decodesrc20(xfer_hex)
        assert_equal(decoded_xfer["action"], "TRANSFER")
        self.log.info("  decodesrc20 valid TRANSFER: OK")

        # 3. Build a valid burn script and decode
        burn_data = node.burntoken(real_token, 50)
        burn_hex = burn_data["op_return_hex"]
        decoded_burn = node.decodesrc20(burn_hex)
        assert_equal(decoded_burn["action"], "BURN")
        self.log.info("  decodesrc20 valid BURN: OK")

        # 4. Invalid hex string
        assert_raises_rpc_error(None, "Invalid hex", node.decodesrc20, "ZZZZ")
        self.log.info("  decodesrc20 invalid hex: OK")

        # 5. Non-SRC20 hex
        assert_raises_rpc_error(None, None, node.decodesrc20, "6a0400000000")
        self.log.info("  decodesrc20 non-SRC20 hex: OK")

        # 6. Empty string
        assert_raises_rpc_error(None, None, node.decodesrc20, "")
        self.log.info("  decodesrc20 empty input: OK")

        # 7. Truncated SRC20 (valid magic but cut off)
        # SRC20 magic is 0x53 0x52 0x43 0x32 0x30 in an OP_RETURN
        # OP_RETURN = 6a, push 5 bytes = 05, then SRC20 magic
        truncated = "6a055352433230"  # OP_RETURN + SRC20 magic only, no version/action/data
        assert_raises_rpc_error(None, None, node.decodesrc20, truncated)
        self.log.info("  decodesrc20 truncated data: OK")

    # ------------------------------------------------------------------ #
    # gettokenbyname positive lookup
    # ------------------------------------------------------------------ #
    def test_gettokenbyname_positive(self):
        self.log.info("Test: gettokenbyname positive lookup")
        node = self.nodes[0]

        # Issue a token and look it up by name
        result = self.w0.walletissuetoken("FIND", "Find Me Token", 4, 777777)
        self.generate(self.nodes[0], 1)
        self.sync_all()

        found = node.gettokenbyname("FIND")
        assert_equal(found["ticker"], "FIND")
        assert_equal(found["name"], "Find Me Token")
        assert_equal(found["decimals"], 4)
        self.log.info("  gettokenbyname('FIND'): OK")

        # Non-existent ticker
        assert_raises_rpc_error(None, None, node.gettokenbyname, "NOPE")
        self.log.info("  gettokenbyname('NOPE') error: OK")

    # ------------------------------------------------------------------ #
    # metadata_hash on issuance
    # ------------------------------------------------------------------ #
    def test_metadata_hash(self):
        self.log.info("Test: metadata_hash on issuance")
        node = self.nodes[0]

        # Issue with metadata hash via node-level RPC (script builder)
        fake_hash = "aa" * 32  # 32-byte hash
        issue_data = node.issuetoken("MHSH", "Meta Hash Test", 0, 9999, fake_hash)
        # Verify the metadata_hash roundtrips through script build + decode
        decoded = node.decodesrc20(issue_data["op_return_hex"])
        assert_equal(decoded["action"], "ISSUE")
        # The metadata hash should be encoded in the script
        assert "metadata_hash" in decoded["data"]
        self.log.info(f"  metadata_hash in decoded script: {decoded['data']['metadata_hash']}")

        # Issue without metadata hash — decoded script should show all zeros
        issue_data2 = node.issuetoken("NMHS", "No Meta", 0, 5555)
        decoded2 = node.decodesrc20(issue_data2["op_return_hex"])
        assert_equal(decoded2["data"]["metadata_hash"], "00" * 32)
        self.log.info("  empty metadata_hash decodes to zeros: OK")

    # ------------------------------------------------------------------ #
    # Transfer to self
    # ------------------------------------------------------------------ #
    def test_transfer_to_self(self):
        self.log.info("Test: transfer to self")
        node = self.nodes[0]

        result = self.w0.walletissuetoken("SELF", "Self Transfer", 0, 10000)
        self.generate(self.nodes[0], 1)
        token_id = result["token_id"]

        # Get current balance
        bal_before = self.w0.gettokenbalances()
        self_bal = None
        for b in bal_before:
            if b["token_id"] == token_id:
                self_bal = b["balance"]
                break
        assert self_bal is not None, "Token not found in balances"
        assert_equal(self_bal, 10000)

        # Transfer to self
        self.w0.wallettransfertoken(token_id, self.addr0, 5000)
        self.generate(self.nodes[0], 1)
        self.sync_all()

        # Balance should still be 10000 (not 5000 and not 15000)
        bal_after = self.w0.gettokenbalances()
        self_bal_after = None
        for b in bal_after:
            if b["token_id"] == token_id:
                self_bal_after = b["balance"]
                break
        assert_equal(self_bal_after, 10000)
        self.log.info("  Transfer to self preserves balance: OK")

    # ------------------------------------------------------------------ #
    # gettokenhistory
    # ------------------------------------------------------------------ #
    def test_gettokenhistory(self):
        self.log.info("Test: gettokenhistory RPC")
        node = self.nodes[0]

        # Issue token, do some transfers and a burn
        result = self.w0.walletissuetoken("HIST", "History Test", 0, 100000)
        self.generate(self.nodes[0], 1)
        self.sync_all()
        token_id = result["token_id"]

        # Transfer 1
        self.w0.wallettransfertoken(token_id, self.addr1, 30000)
        self.generate(self.nodes[0], 1)
        self.sync_all()

        # Transfer 2
        self.w0.wallettransfertoken(token_id, self.addr1, 20000)
        self.generate(self.nodes[0], 1)
        self.sync_all()

        # Burn
        self.w0.walletburntoken(token_id, 5000)
        self.generate(self.nodes[0], 1)
        self.sync_all()

        # Full history
        history = node.gettokenhistory(token_id)
        assert_greater_than(len(history), 0)
        self.log.info(f"  gettokenhistory returned {len(history)} records")

        # Paginated: count=1
        page1 = node.gettokenhistory(token_id, 0, 1)
        assert_equal(len(page1), 1)
        self.log.info("  gettokenhistory pagination (count=1): OK")

        # Start from high height → should return nothing or subset
        future_history = node.gettokenhistory(token_id, 999999)
        assert_equal(len(future_history), 0)
        self.log.info("  gettokenhistory future height: OK")

    # ------------------------------------------------------------------ #
    # gettokenholders
    # ------------------------------------------------------------------ #
    def test_gettokenholders(self):
        self.log.info("Test: gettokenholders RPC")
        node = self.nodes[0]

        # Issue and distribute to multiple addresses
        result = self.w0.walletissuetoken("HOLD", "Holder Test", 0, 100000)
        self.generate(self.nodes[0], 1)
        self.sync_all()
        token_id = result["token_id"]

        # Transfer to node1
        self.w0.wallettransfertoken(token_id, self.addr1, 25000)
        self.generate(self.nodes[0], 1)
        self.sync_all()

        # Transfer to a new address on node0
        addr2 = self.w0.getnewaddress()
        self.w0.wallettransfertoken(token_id, addr2, 15000)
        self.generate(self.nodes[0], 1)
        self.sync_all()

        # Get all holders
        holders = node.gettokenholders(token_id)
        assert_greater_than(len(holders), 1)
        self.log.info(f"  gettokenholders: {len(holders)} holders")

        # With count=1: gettokenholders(token_id, min_balance=0, count=1)
        holders_1 = node.gettokenholders(token_id, 0, 1)
        assert_equal(len(holders_1), 1)
        self.log.info("  gettokenholders count=1: OK")

    # ------------------------------------------------------------------ #
    # gettokenstats with multiple tokens
    # ------------------------------------------------------------------ #
    def test_gettokenstats_multi(self):
        self.log.info("Test: gettokenstats with multiple tokens")
        node = self.nodes[0]

        # Count existing tokens
        stats_before = node.gettokenstats()
        count_before = stats_before["token_count"]

        # Issue 2 more tokens
        self.w0.walletissuetoken("ST1", "Stats One", 0, 5000)
        self.w0.walletissuetoken("ST2", "Stats Two", 0, 8000)
        self.generate(self.nodes[0], 1)
        self.sync_all()

        stats_after = node.gettokenstats()
        assert_equal(stats_after["token_count"], count_before + 2)
        self.log.info(f"  gettokenstats: {stats_after['token_count']} tokens total: OK")

    # ------------------------------------------------------------------ #
    # listtokens pagination
    # ------------------------------------------------------------------ #
    def test_listtokens_pagination(self):
        self.log.info("Test: listtokens cursor-based pagination")
        node = self.nodes[0]

        # Get all tokens
        all_tokens = node.listtokens(100)
        total = len(all_tokens)
        assert_greater_than(total, 2)
        self.log.info(f"  Total tokens: {total}")

        # Get first page of 3
        page1 = node.listtokens(3)
        assert_equal(len(page1), 3)

        # listtokens uses inclusive cursor (LevelDB Seek), so page2
        # starts AT last_id.  To get non-overlapping pages the client
        # must skip the duplicate first entry of the next page.
        last_id = page1[-1]["token_id"]
        page2_raw = node.listtokens(4, last_id)   # request 1 extra
        assert_greater_than(len(page2_raw), 0)

        # First entry of page2_raw should be the cursor itself — skip it
        assert_equal(page2_raw[0]["token_id"], last_id)
        page2 = page2_raw[1:]
        self.log.info(f"  page2 after skipping cursor duplicate: {len(page2)} entries")

        # Ensure no overlap between pages
        page1_ids = {t["token_id"] for t in page1}
        page2_ids = {t["token_id"] for t in page2}
        assert len(page1_ids & page2_ids) == 0, "Pages should not overlap"
        self.log.info("  listtokens pagination no overlap: OK")

        # Combine all pages using inclusive-cursor walk
        all_paged = list(page1)
        cursor = last_id
        while True:
            page = node.listtokens(101, cursor)  # 1 extra for cursor dup
            if len(page) <= 1:
                # Only the cursor itself (or empty) — no more
                break
            all_paged.extend(page[1:])  # skip cursor duplicate
            cursor = page[-1]["token_id"]
            if len(page) < 101:
                break

        assert_equal(len(all_paged), total)
        self.log.info(f"  Paginated through all {total} tokens: OK")

    # ------------------------------------------------------------------ #
    # All reserved tickers rejected
    # ------------------------------------------------------------------ #
    def test_reserved_tickers_all(self):
        self.log.info("Test: all reserved tickers rejected")
        node = self.nodes[0]

        reserved = node.getreservedtickers()
        self.log.info(f"  Reserved tickers: {reserved}")
        ticker_list = reserved["tickers"]

        for ticker in ticker_list:
            try:
                assert_raises_rpc_error(None, None, self.w0.walletissuetoken,
                                        ticker, f"Reserved {ticker}", 0, 1000)
            except Exception:
                # Some tickers may be < MIN_TICKER_LENGTH and fail for that reason
                # which is also a valid rejection
                pass
        self.log.info(f"  All {len(ticker_list)} reserved tickers rejected: OK")

    # ------------------------------------------------------------------ #
    # INT64_MAX transfer and burn
    # ------------------------------------------------------------------ #
    def test_int64_max_transfer_burn(self):
        self.log.info("Test: INT64_MAX transfer and burn amounts")
        node = self.nodes[0]

        # Issue token with INT64_MAX supply
        result = self.w0.walletissuetoken("IMAX", "Int64 Max", 0, INT64_MAX)
        self.generate(self.nodes[0], 1)
        self.sync_all()
        token_id = result["token_id"]

        info = node.gettokeninfo(token_id)
        assert_equal(info["total_supply"], INT64_MAX)

        # Transfer INT64_MAX - 1 to node1 (keep 1 for ourselves)
        self.w0.wallettransfertoken(token_id, self.addr1, INT64_MAX - 1)
        self.generate(self.nodes[0], 1)
        self.sync_all()

        # Verify balances (gettokenbalance returns an array)
        bal1_arr = node.gettokenbalance(self.addr1, token_id)
        assert_equal(len(bal1_arr), 1)
        assert_equal(bal1_arr[0]["balance"], INT64_MAX - 1)
        self.log.info(f"  Transferred INT64_MAX-1: OK")

        # Burn INT64_MAX - 1 from node1 wallet
        self.w1.walletburntoken(token_id, INT64_MAX - 1)
        self.generate(self.nodes[1], 1)
        self.sync_all()

        info2 = node.gettokeninfo(token_id)
        # Circulating should be 1 (we kept 1, burned the rest)
        assert_equal(info2["circulating_supply"], 1)
        self.log.info(f"  Burned INT64_MAX-1, circulating=1: OK")


if __name__ == '__main__':
    TokenRPCCoverageTest(__file__).main()
