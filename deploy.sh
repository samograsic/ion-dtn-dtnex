#!/bin/bash

# DTNEX Deployment Script
# Deploys latest DTNEX version to multiple nodes

# Configuration
USERNAME="pi"
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

# Logging function
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

# Function to deploy to a single node
deploy_to_node() {
    local node=$1
    log "Starting deployment to ${node}..."
    
    # Test SSH connectivity first
    if ! ssh -o ConnectTimeout=10 -o BatchMode=yes "${USERNAME}@${node}" "echo 'SSH connection test'" >/dev/null 2>&1; then
        error "Cannot connect to ${node}. Please check SSH connectivity and keys."
        return 1
    fi
    
    # Execute deployment commands on remote node
    ssh "${USERNAME}@${node}" << 'ENDSSH'
        set -e  # Exit on any error
        
        echo "=== DTNEX Deployment Started ==="
        echo "Node: $(hostname)"
        echo "Time: $(date)"
        echo "User: $(whoami)"
        
        # Change to DTNEX directory
        echo "Changing to DTNEX directory..."
        cd dtn/ion-dtn-dtnex || {
            echo "ERROR: Cannot find dtn/ion-dtn-dtnex directory"
            exit 1
        }
        
        # Show current version before update
        echo "Current version info:"
        if [ -f dtnex ]; then
            ./dtnex --version 2>/dev/null || echo "Could not determine current version"
        else
            echo "DTNEX not currently installed"
        fi
        
        # Git pull latest changes
        echo "Pulling latest changes from git..."
        git pull || {
            echo "ERROR: Git pull failed"
            exit 1
        }
        
        # Clean and build
        echo "Building DTNEX..."
        make clean
        make || {
            echo "ERROR: Make failed"
            exit 1
        }
        
        # Show new version after build
        echo "New version info:"
        ./dtnex --version 2>/dev/null || echo "Could not determine new version"
        
        # Install (requires sudo)
        echo "Installing DTNEX..."
        sudo make install || {
            echo "ERROR: Make install failed"
            exit 1
        }
        
        echo "=== DTNEX Deployment Completed ==="
        echo "Rebooting system in 5 seconds..."
        sleep 5
        
        # Reboot system
        sudo reboot
ENDSSH
    
    local exit_code=$?
    if [ $exit_code -eq 0 ]; then
        success "Deployment to ${node} completed successfully"
    else
        error "Deployment to ${node} failed with exit code ${exit_code}"
        return 1
    fi
}

# Main deployment function
main() {
    log "Starting DTNEX deployment to ${#NODES[@]} nodes"
    log "Nodes: ${NODES[*]}"
    
    # Check if we have SSH keys set up
    if [ ! -f ~/.ssh/id_rsa ] && [ ! -f ~/.ssh/id_ed25519 ]; then
        warning "No SSH keys found. You may need to set up SSH key authentication."
        echo "To set up SSH keys for password-less deployment:"
        echo "1. Generate key: ssh-keygen -t ed25519"
        echo "2. Copy to nodes: ssh-copy-id pi@<node_ip>"
        echo ""
        read -p "Continue anyway? (y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    fi
    
    local failed_nodes=()
    local successful_nodes=()
    
    # Deploy to each node
    for node in "${NODES[@]}"; do
        echo
        log "Processing node: ${node}"
        
        if deploy_to_node "${node}"; then
            successful_nodes+=("${node}")
        else
            failed_nodes+=("${node}")
        fi
        
        # Small delay between nodes
        sleep 2
    done
    
    # Summary
    echo
    echo "========================================="
    log "DEPLOYMENT SUMMARY"
    echo "========================================="
    
    if [ ${#successful_nodes[@]} -gt 0 ]; then
        success "Successfully deployed to ${#successful_nodes[@]} nodes:"
        for node in "${successful_nodes[@]}"; do
            echo "  ✓ ${node}"
        done
    fi
    
    if [ ${#failed_nodes[@]} -gt 0 ]; then
        error "Failed to deploy to ${#failed_nodes[@]} nodes:"
        for node in "${failed_nodes[@]}"; do
            echo "  ✗ ${node}"
        done
        echo
        echo "Please check the failed nodes manually:"
        echo "1. Verify SSH connectivity and authentication"
        echo "2. Check if dtn/ion-dtn-dtnex directory exists"
        echo "3. Ensure git, make, and sudo privileges are available"
        exit 1
    fi
    
    success "All deployments completed successfully!"
    log "All nodes are rebooting and should come online shortly."
}

# Script usage information
usage() {
    echo "DTNEX Deployment Script"
    echo
    echo "Usage: $0 [options]"
    echo
    echo "Options:"
    echo "  -h, --help     Show this help message"
    echo "  -n, --dry-run  Show what would be done without executing"
    echo
    echo "This script deploys DTNEX to the following nodes:"
    for node in "${NODES[@]}"; do
        echo "  - ${USERNAME}@${node}"
    done
    echo
    echo "Requirements:"
    echo "  - SSH key authentication set up for all nodes"
    echo "  - dtn/ion-dtn-dtnex directory exists on each node"
    echo "  - Git, make, and sudo access available on each node"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            usage
            exit 0
            ;;
        -n|--dry-run)
            log "DRY RUN MODE - showing what would be executed:"
            echo
            for node in "${NODES[@]}"; do
                echo "Would deploy to: ${USERNAME}@${node}"
            done
            echo
            echo "Commands that would be executed on each node:"
            echo "  cd dtn/ion-dtn-dtnex"
            echo "  git pull"
            echo "  make clean && make"
            echo "  sudo make install" 
            echo "  sudo reboot"
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

# Run main deployment
main