# Contributing to OpenSY

Thank you for your interest in contributing to OpenSY! This document provides guidelines for contributing to the project.

## 🌟 Ways to Contribute

1. **Code Contributions** - Bug fixes, features, optimizations
2. **Documentation** - Improve guides, translations, tutorials
3. **Testing** - Write tests, report bugs, test releases
4. **Community** - Help users, moderate discussions, spread the word
5. **Security** - Report vulnerabilities (see [SECURITY.md](SECURITY.md))

## 🚀 Getting Started

### Prerequisites

- **C++20 compatible compiler** (GCC 10+, Clang 11+, MSVC 2019+)
- **CMake 3.22+**
- **Git**
- Familiarity with Bitcoin Core development is helpful

### Building from Source

```bash
# Clone the repository
git clone https://github.com/opensyria/SYL.git
cd SYL

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

# Run tests
cd build && ctest --output-on-failure
```

## 📝 Code Style

### C++ Guidelines

- Follow the existing code style (LLVM/Bitcoin Core style)
- Use `.clang-format` for automatic formatting
- Maximum line length: 100 characters
- Use `snake_case` for variables and functions
- Use `PascalCase` for classes and types
- Prefix member variables with `m_`

### Commit Messages

Follow the conventional commits format:

```
type(scope): short description

Longer description if needed.

Fixes #issue_number
```

Types: `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`

Examples:
```
feat(pow): add Argon2id emergency fallback algorithm

fix(wallet): calculate token confirmations correctly

docs(readme): update mining instructions
```

## 🔄 Pull Request Process

### Before Submitting

1. **Fork** the repository
2. **Create a branch** from `opensy-mainnet`
3. **Write tests** for new functionality
4. **Ensure all tests pass**: `./bin/test_opensy`
5. **Run linters**: `./src/.clang-tidy`

### PR Requirements

- [ ] Code compiles without warnings
- [ ] All existing tests pass
- [ ] New code has test coverage
- [ ] Documentation updated if needed
- [ ] Commit messages follow guidelines
- [ ] No merge commits (rebase instead)

### Review Process

1. **Automated checks** run on PR submission
2. **Maintainer review** within 1-2 weeks
3. **Address feedback** and update PR
4. **Squash and merge** when approved

## 🧪 Testing

### Running Tests

```bash
# Unit tests
./build/bin/test_opensy

# Specific test suite
./build/bin/test_opensy --run_test=pow_tests

# Functional tests
cd test/functional
./test_runner.py

# Single functional test
python3 feature_randomx_pow.py
```

### Writing Tests

- Unit tests go in `src/test/`
- Functional tests go in `test/functional/`
- Name test files with `_tests.cpp` suffix
- Use Boost.Test framework for unit tests

## 📚 Documentation

### Code Documentation

- Use Doxygen-style comments for public APIs
- Document complex algorithms inline
- Update `doc/` files when changing behavior

### API Documentation

```cpp
/**
 * Calculate the RandomX hash for a block header.
 *
 * @param header Block header to hash
 * @param keyBlockHash Hash of the key block (determines RandomX key)
 * @return 256-bit RandomX hash
 *
 * @note Thread-safe via RandomX context pool
 * @see GetRandomXKeyBlockHash() for key block selection
 */
uint256 CalculateRandomXHash(const CBlockHeader& header, const uint256& keyBlockHash);
```

## 🔐 Security

**DO NOT** report security vulnerabilities via GitHub issues.

See [SECURITY.md](SECURITY.md) for the responsible disclosure process.

## 📜 License

By contributing, you agree that your contributions will be licensed under the MIT License.

## ✍️ Developer Certificate of Origin (DCO)

OpenSY uses the Developer Certificate of Origin (DCO) to ensure contributors have the right to submit their code.

**All commits must be signed-off** using `git commit -s`:

```
git commit -s -m "feat(tokens): add new validation rule"
```

This adds:
```
Signed-off-by: Your Name <your.email@example.com>
```

By signing off, you certify that you wrote the code or have the right to submit it under the MIT license. See [developercertificate.org](https://developercertificate.org/) for full text.

**Pro tip:** Configure git to always sign-off:
```bash
git config --global format.signoff true
```

## 🌍 Community

- **Website**: https://opensyria.net
- **GitHub**: https://github.com/opensyria/SYL
- **Twitter/X**: @OpenSYcrypto

## ❓ Questions?

Open a GitHub Discussion or reach out to the maintainers.

---

Thank you for contributing to Syria's first blockchain! 🇸🇾
