#!/bin/bash
# Script to send a test message to the DTNEX instance

if [ -z "$1" ] || [ -z "$2" ]; then
    echo "Usage: $0 <destination_node> <service_number> [message]"
    echo "Example: $0 268484801 12160 \"Test message from script\""
    exit 1
fi

DEST_NODE=$1
SERVICE_NUM=$2
MESSAGE=${3:-"Test message from script $(date)"}

echo "Sending message to ipn:$DEST_NODE.$SERVICE_NUM:"
echo "  $MESSAGE"

bpsource ipn:$DEST_NODE.$SERVICE_NUM "$MESSAGE"

echo "Message sent!"