#!/bin/bash
scriptDir="$(dirname "$0")"
source $scriptDir/../../Codes/build/config.txt
codeDir="/Codes/build"

valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --log-file=vg.tp.%p $dir$codeDir/./a.out
