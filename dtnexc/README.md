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