#!/bin/bash

# Runs in a given current directory all .ini files
# Parameter 1(Optional) - Amount of executions per file. Must be a positive number
#====== Do not modify these values =======

codeDir="/Codes/build"
execDir="/Exec"
ResultsDir="/Results"

config_file=$1
group_index=$2

resize_info=$(grep "\[resize$group_index\]" -n $config_file | cut -d ":" -f1)
first_line=$(echo $resize_info | cut -d " " -f1)
last_line=$(echo $resize_info | cut -d " " -f2)
range_lines=$(( last_line - first_line ))
numP=$(head -$last_line $config_file | tail -$range_lines | cut -d ';' -f1 | grep Procs | cut -d '=' -f2)

echo $numP
