#!/bin/bash
scriptDir="$(dirname "$0")"
source $scriptDir/../../Codes/build/config.txt

export EXTRAE_CONFIG_FILE=extrae.xml
export LD_PRELOAD=$EXTRAE_HOME/lib/libmpitrace.so

$PROTEO_BIN
