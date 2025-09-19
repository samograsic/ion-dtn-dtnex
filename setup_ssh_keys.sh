#!/bin/bash

# SSH Key Setup Script for DTNEX Deployment
# Sets up passwordless SSH access to all deployment nodes

USERNAME="pi"
PASSWORD="bumbee3PI"
NODES=(
    "192.168.0.10"
    "192.168.0.88" 
    "192.168.0.50"
    "192.168.0.56"
    "192.168.0.47"
    "doma.grasic.net"
)

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log() {
    echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Check if expect is installed
check_expect() {
    if ! command -v expect >/dev/null 2>&1; then
        error "The 'expect' command is required but not installed."
        echo "Please install it with:"
        echo "  Ubuntu/Debian: sudo apt-get install expect"
        echo "  CentOS/RHEL: sudo yum install expect"
        echo "  macOS: brew install expect"
        exit 1
    fi
}

# Generate SSH key if it doesn't exist
generate_ssh_key() {
    if [ ! -f ~/.ssh/id_ed25519 ] && [ ! -f ~/.ssh/id_rsa ]; then
        log "Generating SSH key..."
        ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519 -N ""
        success "SSH key generated"
    else
        log "SSH key already exists"
    fi
}

# Copy SSH key to a node using expect to handle password
copy_key_to_node() {
    local node=$1
    log "Setting up SSH key for ${USERNAME}@${node}..."
    
    # Use expect to automate ssh-copy-id with password
    expect << EOF
set timeout 30
spawn ssh-copy-id -o StrictHostKeyChecking=no ${USERNAME}@${node}
expect {
    "password:" {
        send "${PASSWORD}\r"
        expect {
            "Number of key(s) added:" {
                puts "Key successfully added to ${node}"
                exit 0
            }
            "ERROR:" {
                puts "Failed to add key to ${node}"
                exit 1
            }
            timeout {
                puts "Timeout adding key to ${node}"
                exit 1
            }
        }
    }
    "All keys were skipped because they already exist on the remote system." {
        puts "Keys already exist on ${node}"
        exit 0
    }
    "Connection refused" {
        puts "Connection refused by ${node}"
        exit 1
    }
    "No route to host" {
        puts "Cannot reach ${node}"
        exit 1
    }
    timeout {
        puts "Connection timeout to ${node}"
        exit 1
    }
}
EOF

    local exit_code=$?
    if [ $exit_code -eq 0 ]; then
        success "SSH key setup completed for ${node}"
        return 0
    else
        error "Failed to setup SSH key for ${node}"
        return 1
    fi
}

# Test SSH connection without password
test_ssh_connection() {
    local node=$1
    log "Testing SSH connection to ${node}..."
    
    if ssh -o ConnectTimeout=10 -o BatchMode=yes "${USERNAME}@${node}" "echo 'SSH test successful'" >/dev/null 2>&1; then
        success "SSH connection test passed for ${node}"
        return 0
    else
        error "SSH connection test failed for ${node}"
        return 1
    fi
}

# Main function
main() {
    log "Setting up SSH keys for DTNEX deployment"
    log "Target nodes: ${NODES[*]}"
    
    check_expect
    generate_ssh_key
    
    local failed_nodes=()
    local successful_nodes=()
    
    # Setup SSH keys for each node
    for node in "${NODES[@]}"; do
        echo
        if copy_key_to_node "${node}"; then
            if test_ssh_connection "${node}"; then
                successful_nodes+=("${node}")
            else
                failed_nodes+=("${node}")
            fi
        else
            failed_nodes+=("${node}")
        fi
    done
    
    # Summary
    echo
    echo "========================================="
    log "SSH KEY SETUP SUMMARY"
    echo "========================================="
    
    if [ ${#successful_nodes[@]} -gt 0 ]; then
        success "SSH keys successfully set up for ${#successful_nodes[@]} nodes:"
        for node in "${successful_nodes[@]}"; do
            echo "  ✓ ${node}"
        done
    fi
    
    if [ ${#failed_nodes[@]} -gt 0 ]; then
        error "Failed to set up SSH keys for ${#failed_nodes[@]} nodes:"
        for node in "${failed_nodes[@]}"; do
            echo "  ✗ ${node}"
        done
        echo
        echo "Please check:"
        echo "1. Network connectivity to failed nodes"
        echo "2. SSH service running on failed nodes"
        echo "3. Correct username/password combination"
        echo "4. Firewall settings"
        return 1
    fi
    
    success "SSH key setup completed for all nodes!"
    log "You can now run ./deploy.sh to deploy DTNEX"
}

# Usage information
usage() {
    echo "SSH Key Setup Script for DTNEX Deployment"
    echo
    echo "Usage: $0 [options]"
    echo
    echo "Options:"
    echo "  -h, --help     Show this help message"
    echo
    echo "This script sets up SSH key authentication for:"
    for node in "${NODES[@]}"; do
        echo "  - ${USERNAME}@${node}"
    done
    echo
    echo "Requirements:"
    echo "  - 'expect' command installed"
    echo "  - Network connectivity to all nodes"
    echo "  - SSH service running on all nodes"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            usage
            exit 0
            ;;
        *)
            error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
    shift
done

# Run main function
main