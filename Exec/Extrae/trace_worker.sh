#!/bin/bash
dir="/home/usuario/Documentos/malleability_benchmark"
codeDir="/Codes/build"

export EXTRAE_CONFIG_FILE=extrae.xml
export LD_PRELOAD=$EXTRAE_HOME/lib/libmpitrace.so

$dir$codeDir/./a.out

