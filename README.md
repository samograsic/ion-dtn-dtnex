# ion-dtn-dtnex
ION-DTN Network Information Exchange Mechanism Bash Scrip

## What it does
DTNEX script helps to distribute information about individual ION-DTN nodes connections with other nodes in the Delay Tolerant Network. The scrips builds up a local Contact Graph in ION of all the other nodes on DTN network that runs this script. 

## Requirements:
* ION-DTN (4.1.0 or higher) with original bpsource and bpsink applications.
* ION Configuration of neighbor nodes (plans and convergence layers)
* Registered at least one ipn endpoint (used to retrive node ID)

## How it works:
The DTNEX script registers additional ipn endpoint on service number 12160. This service number is used to receive network information messages from other nodes using the bpsink service running in a background. The main loop of the DTNEX script periodically (set up using updateInterval value) sends up information about configured ION plan (directly connected DTN nodes – neighbor nodes) to all the node in the plan. After, the script it parses received network information messages from others nodes and updates local ION Contact Graph accordingly. At the same time, received network messages gets forwarded further to other nodes.

## How to use it
The scrip does not require any configuration. It can be simply started by running ./dtnex.sh command. Note: In order to keep the information about the DTN network topology updated, the script needs to be running.

## Format of DTNEX Network Information Messages:
| Type | Name | Description |
| --- | --- | --- |
| str | xmsg | DTNEX message Indentifier |
| str | version | Used version of DTNEX message (currently only 1)|
| str | type | Type of message, “li” used for information of links/connections |
| str | msgOrigin | Origin of DTNEX message |
| str | msgSource | Sender of DTNEX message |
| str | nodeA | Link/Connection information about nodeA |
| str | nodeB |  Link/Connection information about nodeA |
| str | timestamp | *Timestamp of DTNEX message* |
| str | hopcount | *Hopcount of DTNEX message* |
| str | timespan | *Timespan of Link* |
