# DTNEX - DTN Network Information Exchange

DTNEX is a high-performance system for distributing network topology information across Delay Tolerant Networks (DTN). It enables DTN nodes to build and maintain local contact graphs by exchanging information about network connectivity and node metadata.

## Overview

DTNEX operates by having DTN nodes periodically exchange information about their configured contacts (communication opportunities) with other nodes in the network. This allows all nodes to build a comprehensive view of the network topology, which is essential for effective DTN routing.

## Key Features

- **Automated Contact Distribution**: Shares ION-DTN contact information across the network
- **Node Metadata Exchange**: Distributes node descriptions, GPS coordinates, and contact information
- **CBOR Message Protocol**: Efficient binary message format with HMAC authentication
- **Network Topology Visualization**: Optional GraphViz integration for network diagrams
- **Multi-threaded Operation**: Event-driven architecture with background services
- **Security**: HMAC-based message authentication and replay protection
- **High Performance**: Direct ION API integration, no shell command dependencies

## Architecture

### Core Components

1. **Contact Exchange Engine**: Distributes ION contact plan information to neighbor nodes
2. **Metadata Management**: Shares node descriptions, locations, and contact details
3. **Message Authentication**: HMAC-SHA256 with configurable pre-shared keys
4. **Network Visualization**: GraphViz integration for topology diagrams
5. **Bundle Protocol Integration**: Native ION-DTN Bundle Protocol v7 support

### Network Topology Discovery

DTNEX builds network topology through a distributed information exchange process:

1. **Contact Discovery**: Each node periodically broadcasts its configured ION contacts
2. **Information Propagation**: Nodes forward received contact information to their neighbors  
3. **Local Graph Building**: Each node builds a complete network topology view
4. **Routing Integration**: ION uses the distributed contact graph for routing decisions

### Message Protocol

DTNEX uses CBOR (Compact Binary Object Representation) for efficient message encoding:

- **Contact Messages**: Inform nodes about connectivity between node pairs
- **Metadata Messages**: Share node descriptions, GPS coordinates, and operator information
- **Authentication**: HMAC-SHA256 with configurable pre-shared network keys
- **Replay Protection**: Nonce-based duplicate detection and caching

## Installation

### Prerequisites

- **ION-DTN 4.1.0+**: System libraries must be installed (`libbp`, `libici`)
- **OpenSSL Development Libraries**: For HMAC authentication
- **Build Tools**: GCC compiler and development headers
- **Optional**: GraphViz for network visualization

### System Requirements

#### Linux/Ubuntu/Debian
\`\`\`bash
sudo apt update
sudo apt install build-essential libssl-dev
# Optional: For network visualization
sudo apt install graphviz
\`\`\`

#### Raspberry Pi (Raspbian/Debian)
\`\`\`bash
sudo apt update  
sudo apt install build-essential libssl-dev
# Optional: For network visualization
sudo apt install graphviz
\`\`\`

### ION-DTN System Libraries

DTNEX requires ION-DTN system libraries to be installed. Install ION-DTN either:

#### Option 1: From Package Manager (if available)
\`\`\`bash
# Ubuntu/Debian (if ION packages are available)
sudo apt install ion-dtn-dev
\`\`\`

#### Option 2: Build and Install ION from Source
\`\`\`bash
# Download and build ION-DTN
git clone https://github.com/nasa-jpl/ION
cd ION
make install
# This installs system libraries to /usr/local/lib/
\`\`\`

### Building DTNEX (Self-contained)

DTNEX now includes all necessary ION headers and builds without requiring ION source code:

\`\`\`bash
# Clone the repository
git clone https://github.com/samograsic/ion-dtn-dtnex
cd ion-dtn-dtnex

# Build using self-contained build script
./build_standalone.sh

# Optional: Install system-wide
sudo make install
\`\`\`

The self-contained build only requires ION system libraries (`libbp`, `libici`) to be installed - no ION source code needed!

## Configuration

### Basic Configuration File (dtnex.conf)

\`\`\`bash
# DTNEX Configuration File
# DTN Network Information Exchange

# Message exchange interval (seconds) - how often to send contact/metadata updates
updateInterval=30

# Bundle time-to-live should be longer than update interval for reliability  
bundleTTL=1800      # 30 minutes

# Contact lifetime - how long contact information remains valid
contactLifetime=3600  # 1 hour

# Contact time tolerance for clock synchronization issues
contactTimeTolerance=1800  # 30 minutes

# Pre-shared network key for message authentication
presSharedNetworkKey=open

# Node metadata shared with other nodes (max 128 characters)
# Format: "NodeName,ContactInfo,LocationDescription"
# CBOR will create: [nodeId, name, contact] or [nodeId, name, contact, lat, lon] with GPS
nodemetadata="DTNEX-Node,admin@example.com,Test-Location"

# GPS coordinates for enhanced metadata (optional)
# When enabled, CBOR metadata will include GPS coordinates as integers (multiplied by 1000000)
gpsLatitude=59.334591
gpsLongitude=18.063240

# Graph visualization settings
createGraph=true
graphFile=contactGraph.png

# Service mode operation
serviceMode=false    # Set to true for background daemon mode
debugMode=false      # Enable verbose debug output

# Disable metadata exchange if needed
noMetadataExchange=false
\`\`\`

### Configuration Parameters

| Parameter | Description | Default | Example |
|-----------|-------------|---------|---------|
| \`updateInterval\` | Message exchange frequency (seconds) | 30 | 60 |
| \`bundleTTL\` | Bundle time-to-live (seconds) | 1800 | 3600 |
| \`contactLifetime\` | Contact validity duration (seconds) | 3600 | 7200 |
| \`contactTimeTolerance\` | Clock sync tolerance (seconds) | 1800 | 1800 |
| \`presSharedNetworkKey\` | HMAC authentication key | "open" | "mynetwork123" |
| \`nodemetadata\` | Node description string | "" | "Node1,admin@site.com,Location" |
| \`gpsLatitude\` | GPS latitude (decimal degrees) | - | 59.334591 |
| \`gpsLongitude\` | GPS longitude (decimal degrees) | - | 18.063240 |
| \`createGraph\` | Enable GraphViz visualization | false | true |
| \`graphFile\` | Output graph filename | contactGraph.png | /var/www/graph.png |
| \`serviceMode\` | Background daemon mode | false | true |
| \`debugMode\` | Verbose debug output | false | true |
| \`noMetadataExchange\` | Disable metadata sharing | false | true |

## Usage

### Basic Operation

\`\`\`bash
# Start DTNEX (ensure ION is running first)
./dtnex

# Start with debug output
./dtnex --debug

# Background service mode
./dtnex --service
\`\`\`

### Service Integration

#### Systemd Service (Linux)
\`\`\`bash
# Create service file
sudo tee /etc/systemd/system/dtnex.service << EOF
[Unit]
Description=DTNEX - DTN Network Information Exchange
After=ion.service
Requires=ion.service

[Service]
Type=simple
User=dtn
WorkingDirectory=/opt/dtnex
ExecStart=/usr/local/bin/dtnex --service
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
