#!/usr/bin/env python3
# Copyright (c) 2023 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test validateaddress for main chain"""

from test_framework.test_framework import OpenSYTestFramework

from test_framework.util import assert_equal

INVALID_DATA = [
    # BIP 173
    (
        "tc1qw508d6qejxtdg4y5r3zarvary0c5xw7kg3g4ty",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # Invalid hrp
        [],
    ),
    ("syl1qw508d6qejxtdg4y5r3zarvary0c5xw7k6tacwa", "Invalid Bech32 checksum", [42]),
    (
        "BC13W508D6QEJXTDG4Y5R3ZARVARY0C5XW7KN40WF2",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # Invalid HRP for OpenSY
        [],
    ),
    (
        "syl1rw5sgj4rc",
        "Invalid checksum",  # Invalid program length
        [],
    ),
    (
        "syl10w508d6qejxtdg4y5r3zarvary0c5xw7kw508d6qejxtdg4y5r3zarvary0c5xw7kw5rsalpp",
        "Invalid checksum",  # Invalid program length
        [],
    ),
    (
        "BC1QR508D6QEJXTDG4Y5R3ZARVARYV98GJ9P",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # Invalid HRP for OpenSY
        [],
    ),
    (
        "tsyl1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3QL5k7",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # Mixed case
        [],
    ),
    (
        "BC1QW508D6QEJXTDG4Y5R3ZARVARY0C5XW7KV8F3t4",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # Invalid HRP for OpenSY
        [],
    ),
    (
        "syl1zw508d6qejxtdg4y5r3zarvaryvmxvxyq",
        "Invalid checksum",  # Wrong padding
        [],
    ),
    (
        "tsyl1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3ptu70wl",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # tsyl, Non-zero padding in 8-to-5 conversion
        [],
    ),
    ("syl1smsmsx", "Empty Bech32 data section", []),
    # BIP 350
    (
        "tc1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vq5zuyut",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # Invalid human-readable part
        [],
    ),
    (
        "syl1p0x6xa6mejdj6c9ydqa22urq95quqfm6xd3mgr3k2e32rqk3m9gav63ke0",
        "Version 1+ witness address must use Bech32m checksum",  # Invalid checksum (Bech32 instead of Bech32m)
        [],
    ),
    (
        "tsyl1z0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqyewurc",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # tsyl, Invalid checksum (Bech32 instead of Bech32m)
        [],
    ),
    (
        "BC1S0XLXVLHEMJA6C4DQV22UAPCTQUPFHLXM9H8Z3K2E72Q4K9HCZ7VQ54WELL",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # Invalid HRP for OpenSY
        [],
    ),
    (
        "syl1qw508d6qejxtdg4y5r3zarvary0c5xw7k0hd5tj",
        "Version 0 witness address must use Bech32 checksum",  # Invalid checksum (Bech32m instead of Bech32)
        [],
    ),
    (
        "tsyl1q0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqxnh640",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # tsyl, Invalid checksum (Bech32m instead of Bech32)
        [],
    ),
    (
        "syl1p38j9r5y49hruaue7wxjce0updqjuyyx0kh56v8s25huc6995vvzs49mow4",
        "Invalid Base 32 character",  # Invalid character 'o' in checksum
        [60],
    ),
    (
        "BC130XLXVLHEMJA6C4DQV22UAPCTQUPFHLXM9H8Z3K2E72Q4K9HCZ7VQ7ZWS8R",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # Invalid HRP for OpenSY
        [],
    ),
    ("syl1pw5uf7fpw", "Invalid Bech32 address program size (1 byte)", []),
    (
        "syl1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7v8n0nx0muaewav25p5j0ln",
        "Invalid Bech32 address program size (41 bytes)",
        [],
    ),
    (
        "tsyl1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hclm7z4uZagq",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # tsyl, Mixed case
        [],
    ),
    (
        "syl1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7v07q22zl53",
        "Invalid padding in Bech32 data section",  # zero padding of more than 4 bits
        [],
    ),
    (
        "tsyl1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vp3jrxsp",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # tsyl, Non-zero padding in 8-to-5 conversion
        [],
    ),
]
VALID_DATA = [
    # BIP 350
    (
        "syl1qw508d6qejxtdg4y5r3zarvary0c5xw7k6tacws",
        "0014751e76e8199196d454941c45d1b3a323f1433bd6",
    ),
    (
        "syl1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qa9wyvc",
        "00201863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262",
    ),
    (
        "syl1pw508d6qejxtdg4y5r3zarvary0c5xw7kw508d6qejxtdg4y5r3zarvary0c5xw7kcvl4ml",
        "5128751e76e8199196d454941c45d1b3a323f1433bd6751e76e8199196d454941c45d1b3a323f1433bd6",
    ),
    ("syl1sw50qpfueka", "6002751e"),
    ("syl1zw508d6qejxtdg4y5r3zarvaryvlr632f", "5210751e76e8199196d454941c45d1b3a323"),
    (
        "syl1qqqqqp399et2xygdj5xreqhjjvcmzhxw4aywxecjdzew6hylgvses3nx2dz",
        "0020000000c4a5cad46221b2a187905e5266362b99d5e91c6ce24d165dab93e86433",
    ),
    (
        "syl1pqqqqp399et2xygdj5xreqhjjvcmzhxw4aywxecjdzew6hylgvsesmyxr47",
        "5120000000c4a5cad46221b2a187905e5266362b99d5e91c6ce24d165dab93e86433",
    ),
    (
        "syl1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vq8tndjx",
        "512079be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798",
    ),
    # PayToAnchor(P2A)
    (
        "syl1pfeese8ra2x",
        "51024e73",
    ),
]


class ValidateAddressMainTest(OpenSYTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.chain = ""  # main
        self.num_nodes = 1
        self.extra_args = [["-prune=899"]] * self.num_nodes

    def check_valid(self, addr, spk):
        info = self.nodes[0].validateaddress(addr)
        assert_equal(info["isvalid"], True)
        assert_equal(info["scriptPubKey"], spk)
        assert "error" not in info
        assert "error_locations" not in info

    def check_invalid(self, addr, error_str, error_locations):
        res = self.nodes[0].validateaddress(addr)
        assert_equal(res["isvalid"], False)
        assert_equal(res["error"], error_str)
        assert_equal(res["error_locations"], error_locations)

    def test_validateaddress(self):
        for (addr, error, locs) in INVALID_DATA:
            self.check_invalid(addr, error, locs)
        for (addr, spk) in VALID_DATA:
            self.check_valid(addr, spk)

    def run_test(self):
        self.test_validateaddress()


if __name__ == "__main__":
    ValidateAddressMainTest(__file__).main()
