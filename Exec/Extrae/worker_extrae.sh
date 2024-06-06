#!/bin/bash
scriptDir="$(dirname "$0")"
source $scriptDir/../../Codes/build/config.txt
codeDir="/Codes/build"

export EXTRAE_CONFIG_FILE=extrae.xml
export LD_PRELOAD=$EXTRAE_HOME/lib/libmpitrace.so

$dir$codeDir/./a.out
