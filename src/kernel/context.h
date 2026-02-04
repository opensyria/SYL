// Copyright (c) 2022 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENSY_KERNEL_CONTEXT_H
#define OPENSY_KERNEL_CONTEXT_H

namespace kernel {
//! Context struct holding the kernel library's logically global state, and
//! passed to external libopensy_kernel functions which need access to this
//! state. The kernel library API is a work in progress, so state organization
//! and member list will evolve over time.
//!
//! State stored directly in this struct should be simple. More complex state
//! should be stored to std::unique_ptr members pointing to opaque types.
struct Context {
    Context();
};
} // namespace kernel

#endif // OPENSY_KERNEL_CONTEXT_H
