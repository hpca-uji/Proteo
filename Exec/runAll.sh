#!/bin/bash

dir="/home/usuario/Documentos/malleability_benchmark"
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
# FIXME Tener en cuenta que puede ser más de un resize
  aux=$(grep "\[resize0\]" -n $config_file | cut -d ":" -f1)
  ini=$(echo $aux | cut -d " " -f1)
  fin=$(echo $aux | cut -d " " -f2)
  diff=$(( fin - ini ))
  numP1=$(head -$fin $config_file | tail -$diff | cut -d ';' -f1 | grep Procs | cut -d '=' -f2)

  aux=$(grep "\[resize1\]" -n $config_file | cut -d ":" -f1)
  ini=$(echo $aux | cut -d " " -f1)
  fin=$(echo $aux | cut -d " " -f2)
  diff=$(( fin - ini ))
  numP2=$(head -$fin $config_file | tail -$diff | cut -d ';' -f1 | grep Procs | cut -d '=' -f2)

  echo "------------------------------------------run np=$numP1"
  if [ $numP1 -lt $numP2 ]
  then
    numP1=$numP2
  fi

  node_qty=$(($numP1 / $cores))
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
