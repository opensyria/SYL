OpenSY
=====================================

<p align="center">
  <strong>🇸🇾 Syria's First Blockchain</strong><br>
  <em>A cryptocurrency by Syrians, for Syrians</em>
</p>

<p align="center">
  <a href="README.ar.md">🇸🇾 اقرأ بالعربية</a>
</p>

<p align="center">
  <em>"Dec 8 2024 - Syria Liberated from Assad / سوريا حرة"</em><br>
  — Genesis Block Message
</p>

---

## 🌟 What is OpenSY?

**OpenSY (SYL)** is Syria's own digital currency - like Bitcoin, but made specifically for the Syrian people.

Think of it as digital money that:
- 🔒 **Cannot be controlled** by any government or bank
- 🌍 **Works anywhere** in the world with internet
- 💸 **Send money instantly** to family abroad with low fees
- 🏦 **You control your money** - no bank account needed
- 💻 **Anyone can mine** with a regular computer

---

## 🚀 Network Status: LIVE

| | |
|--------|-------|
| **Status** | ✅ Mainnet Active |
| **Block Explorer** | 🔍 [explorer.opensyria.net](https://explorer.opensyria.net) |
| **Website** | 🌐 [opensyria.net](https://opensyria.net) |

---

## 📱 For Regular Users

### Download a Wallet
- **Desktop:** Download from [Releases](https://github.com/opensyria/SYL/releases)
- **Mobile:** Coming soon for iOS and Android
- **Web:** Visit [wallet.opensyria.net](https://wallet.opensyria.net)

---

## 💻 For Developers & Miners

### System Requirements
- **OS:** Linux, macOS, or Windows
- **RAM:** 4GB minimum (8GB recommended)
- **Storage:** 10GB free space

### Build from Source

```bash
# 1. Clone the repository
git clone https://github.com/opensyria/SYL.git
cd SYL

# 2. Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# 3. Run a node
./build/bin/opensyd -daemon

# 4. Check if it's working
./build/bin/opensy-cli getblockchaininfo
```

---

## ⛏️ Mining (Earn SYL)

### Why Mine OpenSY?
- **Fair for everyone** - Uses your regular computer's CPU
- **No expensive equipment** - No need for special mining machines
- **Earn 10,000 SYL** per block you mine

### Start Mining in 3 Steps

```bash
# Step 1: Create a wallet
./build/bin/opensy-cli createwallet "mywallet"

# Step 2: Get your mining address
./build/bin/opensy-cli getnewaddress "mining"
# Save this address! It looks like: syl1qxxxxxxxxx...

# Step 3: Start mining
./build/bin/opensy-cli generatetoaddress 1 YOUR_ADDRESS_HERE
```

> 💡 **Tip:** Leave mining running overnight for better chances!

### Mining Hardware Guide

| Your Computer | Can You Mine? | Performance |
|---------------|---------------|-------------|
| Basic laptop (4 cores) | ✅ Yes | Entry level |
| Gaming PC (8+ cores) | ✅ Yes | Good |
| Server (32+ cores) | ✅ Yes | Excellent |

### Memory Requirements

> ⚠️ **Important:** RandomX mining (blocks 210,000+) requires significant RAM.

| Mode | RAM Required | Hash Rate | When to Use |
|------|--------------|-----------|-------------|
| **Full Dataset** | 2.5GB free | 100% | Recommended for mining |
| **Light Mode** | 256MB | ~10% | Low-memory systems |

**To use light mode** (slower but works on any system):
```bash
opensyd -randomxlightmode
```

**CPU Recommendations:**
- x86_64 processor with AES-NI instruction support
- More cores = faster mining
- ARM64 supported but slower

---

## 📊 Technical Specifications

| Feature | Value |
|---------|-------|
| **Currency Symbol** | SYL |
| **Address Format** | Starts with `F` (Freedom) |
| **Modern Address** | Starts with `syl1` |
| **Network Port** | 9633 (Syria's +963) |
| **Block Time** | ~2 minutes |
| **Block Reward** | 10,000 SYL |
| **Halving** | Every ~4 years |
| **Max Supply** | 21 Billion SYL |
| **Mining Algorithm** | RandomX (CPU-friendly) |
| **Genesis Date** | December 8, 2024 |

---

## 🪙 SRC-20 Tokens

OpenSY supports native tokens via the **SRC-20 standard**. Anyone can issue tokens on the OpenSY blockchain!

### Token Tracking (Built-in)

Token tracking is automatically enabled on all OpenSY nodes - no configuration required. Just start your node and use the token RPCs!

```bash
# Start the node - token tracking is automatic
opensyd
```

### Token Commands

```bash
# List all tokens
opensy-cli listtokens

# Get token info
opensy-cli gettokeninfo <token_id>

# Issue your own token
opensy-cli issuetoken "SYM" "My Token Name" 1000000 8

# Transfer tokens
opensy-cli transfertoken <token_id> <to_address> <amount>
```

📖 See [doc/src20-spec.md](doc/src20-spec.md) for the full SRC-20 specification.

---

## 🔗 Links & Resources

| Resource | Link |
|----------|------|
| 🌐 Website | [opensyria.net](https://opensyria.net) |
| 🔍 Block Explorer | [explorer.opensyria.net](https://explorer.opensyria.net) |
| 💬 Telegram | [t.me/opensyria](https://t.me/opensyria) |
| 📖 Documentation | [docs.opensyria.net](https://docs.opensyria.net) |
| 🐛 Report Issues | [GitHub Issues](https://github.com/opensyria/SYL/issues) |

---

## ❓ Frequently Asked Questions

**How do I get SYL?**
- Mine it yourself (free, uses your computer)
- Receive it from someone else
- Exchange listings coming soon

**Is my money safe?**
- Yes! Keep your wallet backup safe and never share your private keys

**Can I use this in Syria?**
- OpenSY works anywhere with internet. Designed for limited connectivity.

---

## 🤝 Contributing

We welcome contributions from the Syrian community and beyond!
- Report bugs via [GitHub Issues](https://github.com/opensyria/SYL/issues)
- Submit improvements via Pull Requests
- Help translate to more languages

---

## 📄 License

OpenSY is open-source under the MIT license. See [COPYING](COPYING) for details.

---

<p align="center">
  <strong>سوريا حرة</strong> 🇸🇾<br>
  <em>For the people of Syria, by the people of Syria</em>
</p>
