# DTNEX Deployment Guide

This directory contains scripts for automated deployment of DTNEX to multiple nodes.

## Files

- `deploy.sh` - Main deployment script
- `setup_ssh_keys.sh` - SSH key setup helper script
- `DEPLOYMENT.md` - This documentation

## Target Nodes

The deployment scripts target the following nodes:
- 192.168.0.10
- 192.168.0.88
- 192.168.0.50
- 192.168.0.56
- 192.168.0.47
- doma.grasic.net

All connections use username: `pi`

## Quick Start

### Option 1: First-time Setup (Recommended)

1. **Set up SSH keys** (one-time setup):
   ```bash
   ./setup_ssh_keys.sh
   ```
   This will automatically set up passwordless SSH access to all nodes.

2. **Deploy DTNEX**:
   ```bash
   ./deploy.sh
   ```

### Option 2: Manual SSH Key Setup

If you prefer to set up SSH keys manually:

1. **Generate SSH key** (if you don't have one):
   ```bash
   ssh-keygen -t ed25519
   ```

2. **Copy key to each node**:
   ```bash
   ssh-copy-id pi@192.168.0.10
   ssh-copy-id pi@192.168.0.88
   # ... repeat for all nodes
   ```

3. **Deploy DTNEX**:
   ```bash
   ./deploy.sh
   ```

## What the Deployment Does

On each target node, the script:

1. **Updates code**: `git pull` latest changes
2. **Builds**: `make clean && make`
3. **Installs**: `sudo make install`
4. **Reboots**: `sudo reboot`

## Script Options

### deploy.sh

- `./deploy.sh` - Run full deployment
- `./deploy.sh --dry-run` - Show what would be done without executing
- `./deploy.sh --help` - Show help

### setup_ssh_keys.sh

- `./setup_ssh_keys.sh` - Set up SSH keys for all nodes
- `./setup_ssh_keys.sh --help` - Show help

## Requirements

### Local Machine
- `ssh` client
- `expect` command (for automated password entry)
  - Ubuntu/Debian: `sudo apt-get install expect`
  - CentOS/RHEL: `sudo yum install expect`
  - macOS: `brew install expect`

### Target Nodes
- SSH server running
- `git` installed
- `make` and build tools
- `sudo` privileges for user `pi`
- Directory `dtn/ion-dtn-dtnex` exists with git repository

## Troubleshooting

### SSH Connection Issues
- Verify network connectivity: `ping <node_ip>`
- Check SSH service: `ssh pi@<node_ip>`
- Verify SSH key: `ssh-copy-id pi@<node_ip>`

### Build Issues
- Check if `dtn/ion-dtn-dtnex` directory exists on target node
- Verify git repository is properly cloned
- Ensure build dependencies are installed (gcc, make, ION-DTN libraries)

### Permission Issues
- Verify `pi` user has sudo privileges
- Check if `/usr/local/bin` is writable with sudo

## Security Notes

- The `setup_ssh_keys.sh` script contains the password in plaintext for automation
- Consider deleting or securing this script after initial setup
- SSH keys provide secure, passwordless authentication once set up
- All remote operations are performed over encrypted SSH connections

## Manual Deployment

If automatic deployment fails, you can manually deploy to a node:

```bash
ssh pi@<node_ip>
cd dtn/ion-dtn-dtnex
git pull
make clean && make
sudo make install
sudo reboot
```

## Monitoring Deployment

After deployment:
1. Wait for nodes to reboot (1-2 minutes)
2. SSH to each node to verify DTNEX version:
   ```bash
   ssh pi@<node_ip> "dtnex --version"
   ```
3. Check if DTNEX service is running (if applicable)