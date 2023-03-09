#!/bin/bash

dir="/home/martini/malleability_benchmark"
partition="P1"
exclude="c00,c01,c02"
cores=20

# Runs in a given current directory all .ini files
# Parameter 1(Optional) - Amount of executions per file. Must be a positive number
#====== Do not modify these values =======

codeDir="/Codes/build"
execDir="/Exec"
ResultsDir="/Results"
use_extrae=0

qty=1
if [ $# -ge 1 ]
then
  qty=$1
fi

files="./*.ini"
internalIndex=$(echo $files | tr -cd ' ' | wc -c)
index=$((0))
for config_file in $files
do
  max_numP=-1
  total_groups=$(grep Total_Resizes $config_file | cut -d '=' -f2)
  for ((j=0; j<total_groups; j++)); 
  do
    resize_info=$(grep "\[resize$j\]" -n $config_file | cut -d ":" -f1)
    first_line=$(echo $resize_info | cut -d " " -f1)
    last_line=$(echo $resize_info | cut -d " " -f2)
    range_lines=$(( last_line - first_line ))
    numP=$(head -$last_line $config_file | tail -$range_lines | cut -d ';' -f1 | grep Procs | cut -d '=' -f2)
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

  outFileIndex=$(echo $config_file | sed s/[^0-9]//g)
  if [[ $outFileIndex ]]; then 
    index=$outFileIndex
  else 
    index=$internalIndex
    ((internalIndex++))
  fi

  #Execute test
  echo "Execute job $index with Nodes=$node_qty and config_file=$config_file"
  sbatch -p $partition --exclude=$exclude -N $node_qty $dir$execDir/generalRun.sh $dir $config_file $use_extrae $index $qty
done
echo "End"
