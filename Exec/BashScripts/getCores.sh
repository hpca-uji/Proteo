#!/bin/bash

# Obtains the number of total cores in an homogenous partition
# Parameter 1 - Partition to use
#====== Do not modify these values =======
partition=$1

hostlist=$(sinfo -hs --partition $partition | sed 's/  */:/g' | rev | cut -d ':' -f1 | rev)
basic_node=$(scontrol show hostname $hostlist | paste -d, -s | cut -d ',' -f1)
cores=$(scontrol show node $basic_node | grep CPUTot | cut -d '=' -f3 | cut -d ' ' -f1)
echo "$cores"
