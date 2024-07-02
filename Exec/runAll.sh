#!/bin/bash

partition="P1"
exclude="c00,c01,c02"

# Runs in a given current directory all .ini files with the aid of the RMS
# Parameter 1(Optional) - Amount of executions per file. Must be a positive number
# Parameter 2(Optional) - Maximum amount of time in seconds needed by a single execution. Default value is 0, which indicates infinite time. Must be a positive integer.
#====== Do not modify these values =======

scriptDir="$(dirname "$0")"
source $scriptDir/../Codes/build/config.txt
cores=$(bash $PROTEO_HOME$execDir/BashScripts/getCores.sh $partition)
use_extrae=0

qty=1
if [ $# -ge 1 ]
then
  qty=$1
fi

limit_time=$((0))
if [ $# -ge 2 ] #Max time per execution in seconds
then
  limit_time=$(($2 * $qty / 60 + 1))
fi

files="./*.ini"
internalIndex=$(echo $files | tr -cd ' ' | wc -c)
index=$((0))
for config_file in $files
do
  node_qty=$(bash $PROTEO_HOME$execDir/BashScripts/getMaxNodesNeeded.sh $config_file $cores)

  outFileIndex=$(echo $config_file | sed s/[^0-9]//g)
  if [[ $outFileIndex ]]; then 
    index=$outFileIndex
  else 
    index=$internalIndex
    ((internalIndex++))
  fi

  #Execute test
  echo "Execute job $index with Nodes=$node_qty and config_file=$config_file"
  sbatch -p $partition --exclude=$exclude -N $node_qty -t $limit_time $PROTEO_HOME$execDir/generalRun.sh $cores $config_file $use_extrae $index $qty
done
echo "End"
