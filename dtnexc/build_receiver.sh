#!/bin/bash
# Build script for the test receiver

# Compile the test receiver
gcc -Wall -g -I../ione-code/bpv7/include -I../ione-code/ici/include -c test_receive.c -o test_receive.o
# Link with BP and ICI libraries
gcc test_receive.o -o test_receiver -L/usr/local/lib -lbp -lici -lm -lpthread

if [ $? -eq 0 ]; then
    echo "Build complete. Run with: ./test_receiver <service_number>"
    echo "For example: ./test_receiver 12162"
    chmod +x test_receiver
else
    echo "Build failed!"
fi