# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 0.1.x   | :white_check_mark: |

## Reporting a Vulnerability

The OpenSY team takes security vulnerabilities seriously. We appreciate your efforts to responsibly disclose your findings.

### How to Report

**DO NOT** create a public GitHub issue for security vulnerabilities.

Instead, please report security vulnerabilities by emailing:

📧 **security@opensyria.net**

### What to Include

Please include the following information in your report:

1. **Type of vulnerability** (e.g., remote code execution, denial of service, information disclosure)
2. **Full paths of source file(s)** related to the vulnerability
3. **Step-by-step instructions** to reproduce the issue
4. **Proof-of-concept or exploit code** (if possible)
5. **Impact assessment** - what an attacker could achieve

### Response Timeline

- **Acknowledgment**: Within 48 hours
- **Initial Assessment**: Within 7 days
- **Resolution Target**: Within 90 days (depending on severity)

### Severity Classification

| Severity | Description | Example |
|----------|-------------|---------|
| **Critical** | Remote code execution, consensus failure, coin theft | Buffer overflow in P2P handling |
| **High** | Denial of service, significant data leak | Crash via malformed block |
| **Medium** | Local privilege escalation, minor DoS | Wallet file permission issues |
| **Low** | Information disclosure, hardening issues | Debug info leak in logs |

### Safe Harbor

We consider security research conducted in accordance with this policy to be:
- Authorized and legal
- Exempt from DMCA anti-circumvention provisions
- Conducted in good faith

We will not pursue legal action against researchers who:
- Report vulnerabilities responsibly
- Do not exploit vulnerabilities beyond proof-of-concept
- Give us reasonable time to address issues before disclosure

### Bug Bounty

We are establishing a bug bounty program. Details will be announced at:
- Website: https://opensyria.net/security
- Twitter/X: @OpenSYcrypto

Bounty amounts (when available):
- Critical: Up to 50,000 SYL
- High: Up to 20,000 SYL
- Medium: Up to 5,000 SYL
- Low: Up to 1,000 SYL

### PGP Key

For sensitive communications, you may encrypt your report using our PGP key.

```
-----BEGIN PGP PUBLIC KEY BLOCK-----

mQINBGeTBhsBEAC7vJ7t7Ks5S0R3eFBhGmMHBm7R3V3i5F5j7K9Q3mN8vR7sW1eX
zY9aL2cP4fG5hI6kJ0lM8nO1pQ2rS4tU3vW6xY7zA9bC0dE2fG5hJ1kL0mN4oP6r
S8tV0wX2yZ5aB7cD9eF1gH4iJ6kL8mN0oP2qR5sT7uV9wX1yZ3aB5cD7eF9gH2iJ
4kL6mN8oP0qR2sT4uV6wX8yZ0aB2cD4eF6gH8iJ0kL2mN4oP6qR8sT0uV2wX4yZ6
aB8cD0eF2gH4iJ6kL8mN0oP2qR4sT6uV8wX0yZ2aB4cD6eF8gH0iJ2kL4mN6oP8q
R0sT2uV4wX6yZ8aB0cD2eF4gH6iJ8kL0mN2oP4qR6sT8uV0wX2yZ4aB6cD8eF0gH
2iJ4kL6mN8oP0qR2sT4uV6wX8yZ0aB2cD4eF6gH8iJ0kL2mN4oP6qR8sT0uV2wX4
yZ6aB8cD0eF2gH4iJ6kL8mN0oP2qR4sT6uV8wX0yZ2aB4cD6eF8gH0iJ2kL4mN6o
P8qR0sT2uV4wX6yZ8aB0cD2eF4gH6iJ8kL0mN2oP4qR6sT8uV0wX2yZ4aB6cD8eF
=oS1Y
-----END PGP PUBLIC KEY BLOCK-----
```

**Key ID:** `0xOPENSY2024`  
**Fingerprint:** `XXXX XXXX XXXX XXXX XXXX  XXXX XXXX XXXX XXXX XXXX`

> ⚠️ **Note:** The above is a placeholder. The actual PGP key will be published at https://opensyria.net/security
> To generate the real key, run:
> ```bash
> gpg --full-generate-key  # Select RSA 4096, 2 years, security@opensyria.net
> gpg --armor --export security@opensyria.net
> ```

## Security Best Practices for Node Operators

### Recommended Configuration

```bash
# Limit RPC access to localhost
rpcbind=127.0.0.1

# Use strong RPC credentials
rpcuser=<random_string>
rpcpassword=<strong_random_password>

# Enable wallet encryption
encryptwallet "your-strong-passphrase"

# Consider running behind a firewall
# Only expose port 9633 for P2P (if needed)
```

### Network Security

- Keep your node software updated
- Run behind a firewall
- Use Tor for enhanced privacy: `-proxy=127.0.0.1:9050`
- Monitor for unusual activity

## Previous Security Advisories

| Date | Severity | Description | Fixed In |
|------|----------|-------------|----------|
| - | - | No advisories yet | - |

---

Thank you for helping keep OpenSY secure! 🔒
