#!/bin/bash

dir="/home/martini/malleability_benchmark/"

# Creates a directory with all possible and valid combinations of configuration files
#   that can be created from a given complex configuration file.
# Parameter 1: Complex configuration file name.
# Parameter 2: Common output name of the output configuration files. It will be appended an index to each of them.
#====== Do not modify these values =======

codeDir="Codes/"
execDir="Exec/"
ResultsDir="Results/"

complex_file=$1
output_name=$2

python3 $dir$execDir/PythonCodes/read_multiple.py $complex_file $output_name

echo "END TEST"

