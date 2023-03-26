#!/bin/bash

dir="/home/martini/malleability_benchmark" #FIXME Obtain from another way

# Runs in a given current directory all .ini files
# Parameter 1(Optional) - Amount of executions per file. Must be a positive number
#====== Do not modify these values =======

codeDir="/Codes/build"
execDir="/Exec"
ResultsDir="/Results"

config_file=$1
cores=$2

max_numP=-1
total_resizes=$(grep Total_Resizes $config_file | cut -d '=' -f2)
total_groups=$(($total_resizes + 1))
for ((j=0; j<total_groups; j++));
do
  numP=$(bash $dir$execDir/BashScripts/getNumPNeeded.sh $config_file $j)
  if [ "$numP" -gt "$max_numP" ];
  then
    max_numP=$numP
  fi
done
node_qty=$(($max_numP / $cores))
if [ $node_qty -eq 0 ]
then
  node_qty=1
fi

echo $node_qty
