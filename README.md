# ion-dtn-dtnex
ION-DTN Network Information Exchange Mechanism

## What it does
DTNEX helps to distribute information about individual ION-DTN nodes connections with other nodes in the Delay Tolerant Network. The system builds up a local Contact Graph in ION of all the other nodes on DTN network that run this software.

## Implementations
DTNEx is available in two implementations:

1. **Bash Script (dtnex.sh)** - The original implementation
2. **C Implementation (dtnexc)** - A higher-performance native implementation with direct ION API integration

## Requirements:
* ION-DTN (4.1.0 or higher) with original bpsource and bpsink applications
* ION Configuration of neighbor nodes (plans and convergence layers)
* Registered at least one ipn endpoint (used to retrieve node ID)
* Optional: Graphviz graph visualization software (https://graphviz.org/)

## How it works:
DTNEx registers additional ipn endpoint on service number 12160. This service is used to receive network information messages from other nodes using the bpsink (bash) or direct BP API (C). The system periodically sends information about configured ION plans (directly connected DTN nodes – neighbor nodes) to all nodes in the plan. It then parses received network information messages from other nodes and updates the local ION Contact Graph accordingly. At the same time, received network messages are forwarded to other nodes.

## How to use it

### Bash Implementation
The bash script does not require any configuration. It can be simply started by running `./dtnex.sh` command.

### C Implementation
The C implementation offers better performance and direct ION API integration. To use it:

1. Navigate to the dtnexc directory
2. Build the application: `./build.sh`
3. Run the application: `./dtnexc`

Configuration options can be set in `dtnex.conf` file.

**Note:** In order to keep the information about the DTN network topology updated, one of these implementations needs to be running.

## Key Features

### Common Features
- Dynamic contact graph updates
- Loop-prevention mechanisms
- Automatic discovery of network topology
- Contact graph visualization
- Node metadata exchange

### C Implementation Additional Features
- Direct ION API integration (no shell commands)
- Multi-threaded operation with bpecho service
- More efficient memory management
- Improved message verification with configurable pre-shared key

## Contact Graph Visualization
Both implementations can visualize ION's contact graph. Install the Graphviz software package and set the "createGraph" option to true.
![GraphViz](https://raw.githubusercontent.com/samograsic/ion-dtn-dtnex/main/dtnGraphExample.png)

## Message Format
See [DTNEX_MESSAGE_FORMAT.md](DTNEX_MESSAGE_FORMAT.md) for detailed documentation on the message format and protocol specifications.