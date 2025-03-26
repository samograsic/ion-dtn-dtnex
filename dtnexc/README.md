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

### Key Components

#### 1. Bundle Protocol Interface

DTNEXC uses ION's Bundle Protocol (BP) API to send and receive network messages. It registers a specialized endpoint to listen for incoming contacts and metadata.

```c
// Initialize the ION BP system
bp_attach();

// Register and open an endpoint for communication
addEndpoint(endpointId, EnqueueBundle, NULL);
bp_open(endpointId, &sap);
```

#### 2. Plan Management

The application directly queries ION's internal database to retrieve the list of neighbors (plans):

```c
// Get plans from ION's database
Object planElt = sdr_list_first(sdr, bpConstants->plans);
for (planElt = sdr_list_first(sdr, bpConstants->plans); 
     planElt && planElt != 0; 
     planElt = sdr_list_next(sdr, planElt)) {
    // Process each plan...
}
```

#### 3. Contact Processing and Distribution

When contact information is received from another node, DTNEXC:
1. Validates the message using the pre-shared network key
2. Processes the contact information 
3. Updates the local ION contact graph if needed
4. Forwards the information to other nodes

#### 4. Security Measures

Security is implemented through:
- Pre-shared network key authentication using SHA-256 hashing
- Input validation with `checkLine()` to prevent malicious content
- Hash-based message caching to prevent forwarding loops

#### 5. Metadata Exchange

Node metadata (node name, email, location) can be shared across the network:

```c
// Update node metadata in the local database
void updateNodeMetadata(DtnexConfig *config, unsigned long nodeId, const char *metadata) {
    // Store metadata about remote nodes
}
```

### Threading Model

The C implementation uses a multi-threaded approach:
- Main thread processes contacts and sends/forwards messages
- Dedicated thread for the bpecho service for connectivity testing
- Thread synchronization through pthread mutexes

### Performance Considerations

1. The implementation minimizes SDR transactions for better performance
2. Caches plan information to reduce database access
3. Implements message deduplication through hash caching
4. Uses direct ION API calls rather than external commands