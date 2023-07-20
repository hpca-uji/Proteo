#!/bin/bash
dir="/home/martini/malleability_benchmark"
codeDir="/Codes/build"

export EXTRAE_CONFIG_FILE=extrae.xml
export LD_PRELOAD=$EXTRAE_HOME/lib/libmpitrace.so

$dir$codeDir/./a.out
