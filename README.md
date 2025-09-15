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
EOF

# Enable and start the service
sudo systemctl enable dtnex
sudo systemctl start dtnex
```

## CBOR Message Format Specification

DTNEX uses CBOR (Compact Binary Object Representation) for efficient, authenticated message exchange between DTN nodes. The protocol supports two primary message types and can be extended for custom applications.

### Protocol Overview

- **Protocol Version**: 2
- **Message Format**: CBOR arrays with HMAC authentication
- **Authentication**: HMAC-SHA256 (truncated to 64 bits for efficiency)
- **Replay Protection**: 3-byte nonce with origin node tracking
- **Transport**: ION Bundle Protocol v7 (service 12160)

### Message Structure

All DTNEX messages follow this general CBOR array format:

```
[version, type, timestamp, expireTime, origin, from, nonce, messageData, hmac]
```

#### Common Header Fields

| Field | Type | Description | Size |
|-------|------|-------------|------|
| `version` | Integer | Protocol version (currently 2) | 1 byte |
| `type` | Integer | Message type (1=contact, 2=metadata) | 1 byte |
| `timestamp` | Integer | Unix timestamp when message was created | 4 bytes |
| `expireTime` | Integer | Unix timestamp when message expires | 4 bytes |
| `origin` | Integer | Node ID that originally created the message | 4-8 bytes |
| `from` | Integer | Node ID that sent this message (may differ from origin for forwarded messages) | 4-8 bytes |
| `nonce` | Byte String | 3-byte random nonce for replay protection | 3 bytes |
| `messageData` | Array/Map | Type-specific message payload | Variable |
| `hmac` | Byte String | 8-byte HMAC-SHA256 authentication tag | 8 bytes |

### Contact Messages (Type 1)

Contact messages distribute network connectivity information between DTN nodes.

#### CBOR Structure
```
[2, 1, timestamp, expireTime, origin, from, nonce, [nodeA, nodeB, duration, datarate, reliability], hmac]
```

#### Message Data Array
```cbor
[nodeA, nodeB, duration, datarate, reliability]
```

| Field | Type | Description | Example |
|-------|------|-------------|---------|
| `nodeA` | Integer | First node ID in the contact pair | 268484800 |
| `nodeB` | Integer | Second node ID in the contact pair | 268484801 |
| `duration` | Integer | Contact duration in minutes (0-65535) | 1440 |
| `datarate` | Integer | Data rate in bytes per second | 100000 |
| `reliability` | Integer | Reliability factor (typically 1-100) | 100 |

#### Example Contact Message
```json
[
  2,                    // Protocol version
  1,                    // Contact message type
  1694885400,          // Timestamp (Unix epoch)
  1694887200,          // Expire time
  268484800,           // Origin node ID
  268484800,           // From node ID (same as origin if not forwarded)
  h'A1B2C3',           // 3-byte nonce
  [                    // Contact data
    268484800,         // Node A
    268484801,         // Node B  
    1440,              // Duration (24 hours in minutes)
    100000,            // Data rate (100 KB/s)
    100                // Reliability (100%)
  ],
  h'1234567890ABCDEF'  // 8-byte HMAC
]
```

### Metadata Messages (Type 2)

Metadata messages share node descriptions, GPS coordinates, and operator contact information.

#### CBOR Structure
```
[2, 2, timestamp, expireTime, origin, from, nonce, metadataMap, hmac]
```

#### Metadata Map Structure
```cbor
{
  1: nodeId,        // Node identifier
  2: "NodeName",    // Human-readable node name (max 24 chars)
  3: "contact@email.com",  // Contact information (max 24 chars)
  4: latitude,      // Optional: GPS latitude * 1000000 (integer)
  5: longitude      // Optional: GPS longitude * 1000000 (integer)
}
```

| Map Key | Field | Type | Description | Example |
|---------|-------|------|-------------|---------|
| 1 | `nodeId` | Integer | Node identifier | 268484800 |
| 2 | `name` | String | Node name (max 24 chars) | "DTNEX-Gateway" |
| 3 | `contact` | String | Contact info (max 24 chars) | "ops@example.com" |
| 4 | `latitude` | Integer | GPS latitude × 1,000,000 (optional) | 59334591 |
| 5 | `longitude` | Integer | GPS longitude × 1,000,000 (optional) | 18063240 |

#### Example Metadata Message
```json
[
  2,                    // Protocol version
  2,                    // Metadata message type
  1694885400,          // Timestamp
  1694887200,          // Expire time
  268484800,           // Origin node ID
  268484800,           // From node ID
  h'D4E5F6',           // 3-byte nonce
  {                    // Metadata map
    1: 268484800,      // Node ID
    2: "Gateway-Node", // Node name
    3: "admin@net.com",// Contact email
    4: 59334591,       // Latitude (Stockholm)
    5: 18063240        // Longitude (Stockholm)
  },
  h'FEDCBA0987654321'  // 8-byte HMAC
]
```

### Message Authentication

All messages include HMAC-SHA256 authentication using a pre-shared network key.

#### HMAC Calculation
1. **Key Preparation**: Pre-shared network key (configured in `dtnex.conf`)
2. **Message Data**: Everything except the HMAC field
3. **HMAC Computation**: HMAC-SHA256 of message data using the network key
4. **Truncation**: First 8 bytes of the HMAC for space efficiency

#### Replay Protection
- **Nonce**: 3-byte random value included in each message
- **Cache**: Each node maintains a cache of seen (nonce, origin) pairs
- **Validation**: Messages with duplicate (nonce, origin) pairs are rejected

### Message Forwarding

DTNEX implements epidemic-style message forwarding:

1. **Reception**: Node receives and validates message
2. **Processing**: Extracts and stores contact/metadata information
3. **Forwarding**: Forwards message to all neighbors except origin and sender
4. **Flood Control**: Nonce-based duplicate detection prevents loops

### Custom Message Types

The CBOR protocol can be extended for custom applications:

#### Adding New Message Types
1. **Define Type Number**: Choose unused integer (3, 4, 5, ...)
2. **Design Message Data**: Create CBOR array or map structure
3. **Implement Handlers**: Add encoding/decoding functions
4. **Authentication**: Use same HMAC scheme for security

#### Example Custom Message (Type 3)
```json
[
  2,                    // Protocol version
  3,                    // Custom message type
  1694885400,          // Timestamp
  1694887200,          // Expire time
  268484800,           // Origin node ID
  268484800,           // From node ID
  h'789ABC',           // 3-byte nonce
  {                    // Custom data map
    "sensor": "temperature",
    "value": 23.5,
    "unit": "celsius",
    "location": "Building-A"
  },
  h'1122334455667788'  // 8-byte HMAC
]
```

### Performance Characteristics

- **Contact Message Size**: ~45-65 bytes (typical)
- **Metadata Message Size**: ~55-85 bytes (without GPS), ~65-95 bytes (with GPS)
- **Maximum Message Size**: 128 bytes (configurable via `MAX_CBOR_BUFFER`)
- **Authentication Overhead**: 8 bytes HMAC + 3 bytes nonce = 11 bytes
- **Encoding Efficiency**: CBOR provides ~20-40% size reduction vs JSON

### Security Considerations

- **Pre-shared Keys**: Use strong, randomly generated network keys
- **Key Distribution**: Secure key distribution required for network access
- **Message Expiry**: Configure appropriate `contactLifetime` values
- **Nonce Entropy**: Ensure good randomness for nonce generation
- **Replay Window**: Balance cache size with replay protection needs
