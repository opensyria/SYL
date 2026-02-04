// Copyright (c) 2023-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENSY_TEST_UTIL_JSON_H
#define OPENSY_TEST_UTIL_JSON_H

#include <univalue.h>

#include <string_view>

UniValue read_json(std::string_view jsondata);

#endif // OPENSY_TEST_UTIL_JSON_H
