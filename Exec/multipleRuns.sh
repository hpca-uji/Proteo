#!/bin/bash

dir="/home/usuario/Documentos/malleability_benchmark"
codeDir="Codes/"
execDir="Exec/"
ResultsDir="Results/"

complex_file=$1
output_name=$2

python3 $dir$execDir/PythonCodes/read_multiple.py $complex_file $output_name

echo "END TEST"

