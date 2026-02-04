# OpenSY Node Operator Guide

This guide provides comprehensive instructions for running OpenSY infrastructure, including full nodes, DNS seeds, and mining operations.

## Table of Contents

1. [Running a Full Node](#running-a-full-node)
2. [Becoming a DNS Seed Operator](#becoming-a-dns-seed-operator)
3. [Running a Fixed Seed Node](#running-a-fixed-seed-node)
4. [Mining Operations](#mining-operations)
5. [Monitoring and Maintenance](#monitoring-and-maintenance)
6. [Security Best Practices](#security-best-practices)

---

## Running a Full Node

### System Requirements

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| CPU | 2 cores | 4+ cores |
| RAM | 4 GB | 8+ GB |
| Storage | 20 GB SSD | 100 GB SSD |
| Bandwidth | 10 Mbps | 100+ Mbps |
| OS | Ubuntu 22.04+ | Ubuntu 24.04 LTS |

### Installation

```bash
# Clone and build
git clone https://github.com/opensyria/SYL.git
cd SYL
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Install (optional)
sudo cmake --install build
```

### Configuration

Create `~/.opensy/opensy.conf`:

```ini
# Network settings
listen=1
maxconnections=125
maxuploadtarget=5000  # MB per day

# RPC settings (if needed)
server=1
rpcbind=127.0.0.1
rpcuser=your_username
rpcpassword=your_secure_password

# Performance
dbcache=450
par=2

# Logging
debuglogfile=/var/log/opensyd/debug.log
```

### Running as a Service

Create `/etc/systemd/system/opensyd.service`:

```ini
[Unit]
Description=OpenSY Node
After=network.target

[Service]
Type=forking
User=opensy
Group=opensy
ExecStart=/usr/local/bin/opensyd -daemon -conf=/etc/opensy/opensy.conf
ExecStop=/usr/local/bin/opensy-cli stop
Restart=on-failure
RestartSec=30
TimeoutStopSec=300

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl enable opensyd
sudo systemctl start opensyd
```

---

## Becoming a DNS Seed Operator

DNS seeds are critical infrastructure for peer discovery. New nodes query DNS seeds to find initial peers.

### Prerequisites

1. **Stable full node** running for 30+ days with 99.5%+ uptime
2. **Server** with static IP and 100+ Mbps bandwidth
3. **Domain name** you control
4. **Commitment** to maintain service for 6+ months

### Setup Steps

#### 1. Set up DNS records

Configure your domain's DNS:

```
seed.yourdomain.com     NS      vps.yourdomain.com
vps.yourdomain.com      A       YOUR_SERVER_IP
```

#### 2. Install the seeder

```bash
# Clone the seeder repository
git clone https://github.com/opensyria/opensy-seeder.git
cd opensy-seeder

# Build
make

# Run
./dnsseed -h seed.yourdomain.com \
          -n vps.yourdomain.com \
          -m you@example.com \
          -p 9633
```

#### 3. Verify operation

```bash
# Test DNS resolution
nslookup seed.yourdomain.com

# Should return multiple IP addresses of OpenSY nodes
```

#### 4. Apply for inclusion

Open an issue using the [Seed Application Template](https://github.com/opensyria/SYL/issues/new?template=seed_application.yml).

### Seeder Maintenance

- **Monitor uptime**: Use UptimeRobot or similar
- **Keep software updated**: Pull latest seeder code regularly
- **Check logs**: Ensure seeder is discovering nodes correctly
- **Respond to issues**: Address any reported problems within 48 hours

---

## Running a Fixed Seed Node

Fixed seeds are hardcoded IP addresses used when DNS seeds are unavailable.

### Requirements

- **Static IP address** (not behind NAT)
- **Port 9633 open** to the internet
- **99.5%+ uptime** commitment
- **Diverse hosting** from existing seeds

### Application Process

1. Run a stable node for 30+ days
2. Contact maintainers via GitHub issue
3. Provide proof of uptime and hosting details
4. If approved, your IP will be added to `src/chainparamsseeds.h`

---

## Mining Operations

### Hardware Requirements

| Mode | RAM | Performance | Use Case |
|------|-----|-------------|----------|
| Full Dataset | 2.5 GB | 100% | Serious mining |
| Light Mode | 256 MB | ~10% | Testing only |

### CPU Recommendations

- **Best**: AMD EPYC, Intel Xeon (high core count)
- **Good**: AMD Ryzen, Intel Core (8+ cores)
- **Acceptable**: Any x86_64 with AES-NI

### Starting Mining

```bash
# Solo mining
opensy-cli generatetoaddress 1 YOUR_ADDRESS

# Continuous mining (in screen/tmux)
while true; do
    opensy-cli generatetoaddress 1 YOUR_ADDRESS
    sleep 1
done
```

### Mining Pool Operation

For running a mining pool, see the [Mining Pool Guide](MINING_POOL_GUIDE.md).

---

## Monitoring and Maintenance

### Key Metrics to Monitor

```bash
# Block height
opensy-cli getblockcount

# Peer connections
opensy-cli getconnectioncount

# Network info
opensy-cli getnetworkinfo

# Memory pool
opensy-cli getmempoolinfo

# RandomX pool status (for miners)
opensy-cli getrandomxpoolinfo
```

### Recommended Monitoring Tools

- **Prometheus + Grafana**: For metrics visualization
- **UptimeRobot**: For uptime monitoring
- **Logwatch**: For log analysis

### Log Management

```bash
# Rotate logs
logrotate /etc/logrotate.d/opensyd

# Example logrotate config
/var/log/opensyd/*.log {
    weekly
    rotate 4
    compress
    delaycompress
    missingok
    notifempty
}
```

---

## Security Best Practices

### Network Security

```bash
# Firewall rules (UFW example)
sudo ufw allow 9633/tcp  # P2P
sudo ufw deny 9634/tcp   # RPC (localhost only)
```

### RPC Security

```ini
# In opensy.conf
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
rpcuser=<random_string>
rpcpassword=<32_character_random_password>
```

### Wallet Security

```bash
# Encrypt wallet
opensy-cli encryptwallet "your-strong-passphrase"

# Backup wallet
opensy-cli backupwallet /path/to/backup/wallet.dat
```

### System Hardening

- Keep OS updated
- Use SSH keys, disable password auth
- Enable automatic security updates
- Run node as non-root user
- Use fail2ban for SSH protection

---

## Troubleshooting

### Node Won't Sync

```bash
# Check peer connections
opensy-cli getpeerinfo

# Force resync
opensyd -reindex
```

### Out of Memory

```bash
# Reduce cache
opensyd -dbcache=100

# Use light mode for RandomX
opensyd -randomxlightmode
```

### Connection Issues

```bash
# Check if port is open
nc -zv your-ip 9633

# Add manual peers
opensy-cli addnode "seed.opensyria.net" "add"
```

---

## Getting Help

- **Documentation**: https://docs.opensyria.net
- **GitHub Issues**: https://github.com/opensyria/SYL/issues
- **Telegram**: https://t.me/opensyria

---

*Thank you for supporting the OpenSY network! 🇸🇾*
