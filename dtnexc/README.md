# DTNEXC

C Implementation of DTNEX (DTN Network Information Exchange Mechanism) for ION-DTN

## Overview

DTNEXC is a C implementation of the DTNEX bash script by Samo Grasic. It provides the same functionality as the original script but as a native C application that directly interfaces with ION-DTN.

## Features

- Distributes information about ION-DTN node connections
- Builds a local Contact Graph of all nodes on the DTN network
- Exchanges metadata about nodes
- Optional visualization of the contact graph (requires Graphviz)

## Requirements

- ION-DTN (4.1.0 or higher)
- ION Configuration of neighbor nodes (plans and convergence layers)
- Registered at least one ipn endpoint (used to retrieve node ID)
- OpenSSL development libraries (for SHA-256 hashing)
- Optional: Graphviz graph visualization software (https://graphviz.org/)

## Building

```bash
# First make sure ION-DTN is installed and configured
# The ION development headers should be available

# Build the application
cd dtnexc
make

# Optionally install
sudo make install
```

## Configuration

The application uses the same configuration file as the bash script version:

```
# dtnex.conf example
updateInterval=30
contactLifetime=3600
contactTimeTolerance=1800
bundleTTL=3600
presSharedNetworkKey="open"
createGraph=true
graphFile="contactGraph.png"
nodemetadata="OpenIPN Node,email@example.com,Location"
```

## Running

```bash
# Make sure ION is running first
./dtnexc
```

## How it works

1. Registers an additional ipn endpoint on service number 12160
2. Periodically sends information about configured ION plans to all nodes
3. Parses received network messages and updates local ION Contact Graph
4. Forwards received messages to other nodes
5. Optionally generates a visualization of the contact graph

## Contact Graph Visualization

If the "createGraph" option is set to true, the application will generate a graph visualization using Graphviz.

## License

Same as the original DTNEX bash script.

## Code Documentation

### Architecture

DTNEXC is built as a standalone C application that interfaces directly with ION-DTN's Bundle Protocol (BP) implementation. The core architecture consists of:

1. **Configuration Management**: Loads and parses settings from dtnex.conf
2. **ION-DTN Integration**: Directly interfaces with ION's internal API
3. **Bundle Reception**: Listens for incoming BP bundles containing contact information
4. **Contact Management**: Processes and forwards contact information
5. **Node Metadata Exchange**: Shares information about DTN nodes
6. **Graph Visualization**: Generates network topology visualizations

# DTNEX Contact Message Format Specification

## Overview
The DTNEX (DTN Exchange) system exchanges contact information and metadata between DTN nodes using a simple, standardized message format. This document describes the message format, validation mechanism, and processing flow for these messages.

## Message Types
DTNEX supports two primary message types:
1. **Contact Messages** (type 'c'): Inform nodes about contact opportunities between pairs of nodes
2. **Metadata Messages** (type 'm'): Provide additional information about nodes in the network

## Message Format

### General Structure
All DTNEX messages follow this general format:
```
<hash> <version> <type> <expire_time> <origin_node> <from_node> <additional_fields...>
```

Where:
- `<hash>`: A 10-character SHA-256 hash for message authentication (first 10 chars)
- `<version>`: Message protocol version (currently "1")
- `<type>`: Message type - "c" for contact, "m" for metadata
- `<expire_time>`: Unix timestamp when the message expires
- `<origin_node>`: Node ID of the message originator
- `<from_node>`: Node ID that forwarded this message
- `<additional_fields...>`: Fields specific to the message type

### Contact Message Format
A contact message (type 'c') has the format:
```
<hash> 1 c <expire_time> <origin_node> <from_node> <node_a> <node_b>
```

Example:
```
a1b2c3d4e5 1 c 1709251200 268484800 268484800 268484800 268484801
```

This message indicates:
- Hash: a1b2c3d4e5 (for validation)
- Version: 1
- Type: c (contact)
- Expires: 1709251200 (Unix timestamp)
- Origin: 268484800 (node that created this contact)
- From: 268484800 (node that sent this message)
- NodeA: 268484800 (first endpoint of contact)
- NodeB: 268484801 (second endpoint of contact)

Meaning: There's a contact opportunity between nodes 268484800 and 268484801, valid until the expire time.

### Metadata Message Format
A metadata message (type 'm') has the format:
```
<hash> 1 m <expire_time> <origin_node> <from_node> <metadata>
```

Example:
```
f6g7h8i9j0 1 m 1709251200 268484800 268484800 OpenIPN Node,samo@grasic.net,Sweden
```

This message indicates:
- Hash: f6g7h8i9j0 (for validation)
- Version: 1
- Type: m (metadata)
- Expires: 1709251200 (Unix timestamp)
- Origin: 268484800 (node that this metadata describes)
- From: 268484800 (node that sent this message)
- Metadata: "OpenIPN Node,samo@grasic.net,Sweden" (information about the node)

## Message Validation

### Hash Generation
The hash is generated using SHA-256 with a pre-shared key:

1. For contact messages:
   ```
   hash = sha256(key + "1 c <expire_time> <origin_node> <node_a> <node_b>")
   ```

2. For metadata messages:
   ```
   hash = sha256(key + "1 m <expire_time> <origin_node> <metadata>")
   ```

Only the first 10 characters of the hash are included in the message.

### Validation Process
When a node receives a message:
1. It extracts the hash from the message
2. It reconstructs the message content (without the hash and current 'from' field)
3. It calculates the expected hash using the pre-shared key
4. If the hashes match, the message is processed
5. If the hashes don't match, the message is rejected

## Message Processing

### Contact Message Processing
When a valid contact message is received:
1. The receiving node creates a bidirectional contact in ION between NodeA and NodeB:
   - Contact from NodeA to NodeB with expiration time
   - Contact from NodeB to NodeA with expiration time
2. The node creates corresponding range entries in ION
3. The node forwards the message to neighbors (except the origin and sender)

### Metadata Message Processing
When a valid metadata message is received:
1. The node updates its local metadata database for the origin node
2. The node stores this information in a file (nodesmetadata.txt)
3. If graph visualization is enabled, the metadata is included in node labels
4. The node forwards the message to neighbors (except the origin and sender)

## Loop Prevention
To prevent message loops, DTNEX implements two mechanisms:

1. **Hash Caching**:
   - Each node maintains a cache of recently seen message hashes
   - If a received message's hash matches one in the cache, the message is ignored
   - Hashes are stored in memory and in a file (receivedHashes.txt)

2. **From Field Update**:
   - When forwarding a message, the node replaces the 'from_node' field with its own ID
   - This allows recipients to avoid sending the message back to recent forwarders

## Message Expiration
Messages include an expiration timestamp to prevent outdated information from circulating. When processing a message, nodes check if the current time exceeds the expiration time. Expired messages are ignored.

## Implementation Notes
- Maximum metadata length is limited to 128 characters in C implementation (32 in Bash)
- Contact times typically default to 3600 seconds (1 hour)
- The pre-shared network key defaults to "open" but should be configured in production