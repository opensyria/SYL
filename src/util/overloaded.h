// Copyright (c) 2021-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENSY_UTIL_OVERLOADED_H
#define OPENSY_UTIL_OVERLOADED_H

namespace util {
//! Overloaded helper for std::visit. This helper and std::visit in general are
//! useful to write code that switches on a variant type. Unlike if/else-if and
//! switch/case statements, std::visit will trigger compile errors if there are
//! unhandled cases.
//!
//! Implementation comes from and example usage can be found at
//! https://en.cppreference.com/w/cpp/utility/variant/visit#Example
template<class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };

//! Deduction guide (required for C++17, optional in C++20 but needed for some compilers)
template<class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

} // namespace util

#endif // OPENSY_UTIL_OVERLOADED_H
