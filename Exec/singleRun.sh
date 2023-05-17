#!/bin/bash

dir="/home/usuario/Documentos/malleability_benchmark"
partition="P1"
exclude="c00,c01,c02"

# Executes a given configuration file with the aid of
# the RMS Slurm.
# Parameter 1: Configuration file name for the emulation.
# Parameter 2(Optional): Index to use for the output files. Must be a positive integer.
# Parameter 3(Optional): Number of repetitions to perform. Must be a positive integer.
# Parameter 4(Optional): Use Extrae(1) or not(0).
# Parameter 5(Optional): Maximum amount of time in seconds needed by a single execution. Default value is 0, which indicates infinite time. Must be a positive integer.
# Parameter 6(Optional): Path where the output files should be saved. 
#====== Do not modify these values =======

codeDir="/Codes/build"
execDir="/Exec"
ResultsDir="/Results"
cores=$(bash $dir$execDir/BashScripts/getCores.sh $partition)

if [ $# -lt 1 ]
then
  echo "Not enough arguments. Usage:"
  echo "bash singleRun.sh config.ini [outFileIndex] [Qty] [Use extrae] [Output path]"
  exit 1
fi

#$1 == configFile
#$2 == outFileIndex
#$3 == Qty of repetitions
#$4 == Use extrae NO(0) YES(1)
#$5 == Max time per execution(s)
#$6 == Output path

config_file=$1
outFileIndex=0
qty=1
use_extrae=0

if [ $# -ge 2 ]
then
  outFileIndex=$2
fi
if [ $# -ge 3 ]
then
  qty=$3
fi
if [ $# -ge 4 ]
then
  use_extrae=$4
fi
limit_time=$((0))
if [ $# -ge 5 ] #Max time per execution in seconds
then
  limit_time=$(($5 * $qty / 60 + 1))
fi
if [ $# -ge 6 ]
then
  output=$6
fi

#Obtain amount of nodes neeeded
node_qty=$(bash $dir$execDir/BashScripts/getMaxNodesNeeded.sh $config_file $dir $cores)
#Run with the expected amount of nodes
sbatch -p $partition --exclude=$exclude -N $node_qty -t $limit_time $dir$execDir/generalRun.sh $dir $cores $config_file $use_extrae $outFileIndex $qty

if ! [ -z "$output" ]
then
  mkdir -p $output
  echo "Moving data to $output\nMoved files:"
  ls R${outFileIndex}_G*
  mv R${outFileIndex}_G* $output
  if [ "$use_extrae" -eq 1 ]
  then
    mv a.out.* $output
    mv TRACE* $output
    mv set-0/ $output
  fi
fi
