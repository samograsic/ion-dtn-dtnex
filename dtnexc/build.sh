#!/bin/bash
# Build script for DTNEXC

# Check for ION headers in the local ione-code directory
if [ ! -d "../ione-code/bpv7/include" ] || [ ! -d "../ione-code/ici/include" ]; then
    echo "ION-DTN headers not found in ../ione-code/bpv7/include or ../ione-code/ici/include"
    echo "Please ensure the ione-code directory contains the ION source code"
    exit 1
fi

# Check for OpenSSL development libraries
if ! pkg-config --exists openssl; then
    echo "OpenSSL development libraries not found"
    echo "Please install libssl-dev package first"
    exit 1
fi

# Build the application
make clean
make

echo "Build complete. The dtnexc binary is located in the current directory."
echo "To install system-wide, run: sudo make install"

# Copy the example configuration if needed
if [ ! -f "../dtnex.conf" ]; then
    echo "Creating example dtnex.conf file..."
    cat > "../dtnex.conf" << EOF
# DTNEX Configuration File
updateInterval=30
contactLifetime=3600
contactTimeTolerance=1800
bundleTTL=3600
presSharedNetworkKey="open"
createGraph=true
graphFile="contactGraph.png"
nodemetadata="OpenIPN Node,example@email.com,Location"
EOF
    echo "Example dtnex.conf created."
fi

echo ""
echo "To run the application:"
echo "  cd .."
echo "  ./dtnexc/dtnexc"
echo ""
echo "Make sure to edit dtnex.conf according to your needs."