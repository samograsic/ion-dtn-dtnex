#!/bin/bash

# Define the source and destination paths
source_path="dtnex.sh"
destination_path="pi@192.168.0.10:/home/pi/dtn/dtnex.sh"

# Copy the script to the remote machine
scp "$source_path" "$destination_path"
scp -P 12162 "$source_path" pi@doma.grasic.net:/home/pi/dtn/dtnex.sh
 

