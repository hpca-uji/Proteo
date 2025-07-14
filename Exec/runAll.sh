#!/bin/bash

# Runs in a given current directory all .ini files with the aid of the RMS
# Parameter 1: Partition name.
# Parameter 2(Optional) - Amount of executions per file. Must be a positive number
# Parameter 3(Optional) - Maximum amount of time in seconds needed by a single execution. Default value is 0, which indicates infinite time. Must be a positive integer.
#====== Do not modify these values =======

scriptDir="$(dirname "$0")"
source $scriptDir/../Codes/build/config.txt
use_extrae=0

if [ $# -lt 1 ]
then
  echo "Not enough arguments. Usage:"
  echo "bash runAll.sh partition [Qty] [Time]"
  exit 1
fi

#$1 == Partition Name
#$2 == Qty of repetitions
#$3 == Max time per execution(s)

partition=$1
qty=1
if [ $# -ge 2 ]
then
  qty=$2
fi

limit_time=$((0))
if [ $# -ge 3 ] #Max time per execution in seconds
then
  limit_time=$(($3 * $qty / 60 + 1))
fi

files="./*.ini"
internalIndex=$(echo $files | tr -cd ' ' | wc -c)
index=$((0))
for config_file in $files
do
  result=$(bash $PROTEO_HOME$execDir/BashScripts/getMaxNodesNeeded.sh $config_file $partition)
  node_qty=$(echo $result | cut -d ',' -f1)
  constraint=$(echo $result | cut -d ',' -f2)

  outFileIndex=$(echo $config_file | sed s/[^0-9]//g)
  if [[ $outFileIndex ]]; then 
    index=$outFileIndex
  else 
    index=$internalIndex
    ((internalIndex++))
  fi

  #Execute test
  echo "Execute job $index with Nodes=$node_qty, Constraints=$constraint and config_file=$config_file"
  sbatch -p $partition -N $node_qty --constraint="$constraint" -t $limit_time $PROTEO_HOME$execDir/generalRun.sh $config_file $use_extrae $index $qty
done
echo "End"
