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

## C Implementation Structure

The C implementation (`dtnexc`) is structured as follows:

### Core Components

1. **Configuration Handling**:
   - `DtnexConfig` structure stores runtime configuration
   - Configuration loaded from `dtnex.conf` (if present)
   - Default values used if no config file is found

2. **Node Administration**:
   - Direct access to ION's routing tables via ION API
   - Manages contacts, ranges, and endpoints

3. **Communication**:
   - Uses BP's bundle sending and receiving functionality
   - Manages sending and receiving bundles
   - Handles dynamic endpoint registration

4. **Message Processing**:
   - Hash verification for security
   - Message parsing and type determination
   - Specialized handlers for different message types

5. **Metadata Management**:
   - In-memory metadata storage for all known nodes
   - Optional persistence to file

### Key Data Structures

1. **DtnexConfig**: 
   ```c
   typedef struct {
       int updateInterval;
       int contactLifetime;
       int contactTimeTolerance;
       int bundleTTL;
       char presSharedNetworkKey[64];
       char serviceNr[16];
       char bpechoServiceNr[16];
       unsigned long nodeId;
       char nodemetadata[MAX_METADATA_LENGTH];
       int createGraph;
       char graphFile[256];
       int noMetadataExchange;
   } DtnexConfig;
   ```

2. **Plan**: 
   ```c
   typedef struct {
       unsigned long planId;
       time_t timestamp;
   } Plan;
   ```

3. **HashCache**: 
   ```c
   typedef struct {
       char hash[20];
       time_t timestamp;
   } HashCache;
   ```

4. **NodeMetadata**: 
   ```c
   typedef struct {
       unsigned long nodeId;
       char metadata[MAX_METADATA_LENGTH];
   } NodeMetadata;
   ```

### Primary Functions

1. **Initialization**:
   - `loadConfig(DtnexConfig *config)`: Loads configuration from file
   - `init(DtnexConfig *config)`: Initializes BP connection and endpoints

2. **Message Handling**:
   - `processMessage(DtnexConfig *config, char *buffer)`: Main message processor
   - `processContactMessage(...)`: Processes contact type messages
   - `processMetadataMessage(...)`: Processes metadata type messages
   - `forwardContactMessage(...)`: Forwards messages to other nodes

3. **Hash and Verification**:
   - `hashString(const char *input, char *output, const char *key)`: Calculates SHA-256 hash
   - `checkLine(char *line)`: Performs security checks on message content

4. **Network Operations**:
   - `exchangeWithNeighbors(...)`: Sends contact and metadata to neighbors
   - `getplanlist(...)`: Gets current plans from ION
   - `sendBundle(...)`: Sends a BP bundle
   - `checkForIncomingBundles(...)`: Checks for incoming bundles

5. **Metadata Management**:
   - `updateNodeMetadata(...)`: Updates the in-memory metadata list
   - `createGraph(...)`: Generates a visualization of the network

### Threading Model

The C implementation uses two threads:

1. **Main Thread**:
   - Handles periodic contact and metadata exchange
   - Processes incoming messages
   - Manages the overall application lifecycle

2. **BPEcho Thread**:
   - Runs the echo service (bpecho) for network testing
   - Responds to echo requests from other nodes

### Configuration Options

Key configuration options in `dtnex.conf`:

```
updateInterval=30           # Time between neighbor exchanges (seconds)
contactLifetime=3600        # How long contacts remain valid (seconds)
contactTimeTolerance=1800   # Tolerance for clock sync issues (seconds)
bundleTTL=3600              # Bundle Time-To-Live (seconds)
presSharedNetworkKey="open" # Shared key for hash verification
nodemetadata="NodeName,contact@example.com,Location" # Node's metadata
createGraph=true            # Whether to generate network graphs
graphFile="contactGraph.png" # Path for generated graph files
noMetadataExchange=false    # Disable metadata sharing if true
```

## Conclusion
This message format enables efficient exchange of contact and metadata information in delay-tolerant networks. The combination of hashing for authentication, loop prevention mechanisms, and expiration times makes the system robust against common networking issues while keeping the overhead minimal.

The C implementation provides performance advantages over the Bash implementation while maintaining compatibility with the message format specifications. It adds additional features such as multi-threading, direct ION API integration, and improved memory management.