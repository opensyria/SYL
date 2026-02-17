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

> ⚠️ **Note:** A real PGP key has not yet been published. Until it is available
> at https://opensyria.net/security, please encrypt sensitive reports using
> the security team's email address with standard email encryption, or
> coordinate key exchange over a verified channel.
>
> To generate the production key, the security team should run:
> ```bash
> gpg --full-generate-key  # Select RSA 4096, 2 years, security@opensyria.net
> gpg --armor --export security@opensyria.net
> ```
> Then replace this section with the actual public key block.

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
