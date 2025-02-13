#!/bin/bash

# Executes a given configuration file with the aid of
# the RMS Slurm.
# Parameter 1: Configuration file name for the emulation.
# Parameter 2: Partition name.
# Parameter 3(Optional): Index to use for the output files. Must be a positive integer.
# Parameter 4(Optional): Number of repetitions to perform. Must be a positive integer.
# Parameter 5(Optional): Use Valgrind(1), Extrae(2) or nothing(0).
# Parameter 6(Optional): Maximum amount of time in seconds needed by a single execution. Default value is 0, which indicates infinite time. Must be a positive integer.
# Parameter 7(Optional): Path where the output files should be saved. 
#====== Do not modify these values =======

scriptDir="$(dirname "$0")"
source $scriptDir/../Codes/build/config.txt

if [ $# -lt 2 ]
then
  echo "Not enough arguments. Usage:"
  echo "bash singleRun.sh config.ini partition [outFileIndex] [Qty] [Use extrae] [Time] [Output path]"
  exit 1
fi

#$1 == configFile
#$2 == Partition Name
#$3 == outFileIndex
#$4 == Qty of repetitions
#$5 == Use external NO(0) Valgrind(1), Extrae(2)
#$6 == Max time per execution(s)
#$7 == Output path

config_file=$1
partition=$2
outFileIndex=0
qty=1
use_external=0

if [ $# -ge 3 ]
then
  outFileIndex=$3
fi
if [ $# -ge 4 ]
then
  qty=$4
fi
if [ $# -ge 5 ]
then
  use_external=$5
fi
limit_time=$((0))
if [ $# -ge 6 ] #Max time per execution in seconds
then
  limit_time=$(($6 * $qty / 60 + 1))
fi
if [ $# -ge 7 ]
then
  output=$7
fi

cores=$(bash $PROTEO_HOME$execDir/BashScripts/getCores.sh $partition)
#Obtain amount of nodes neeeded
node_qty=$(bash $PROTEO_HOME$execDir/BashScripts/getMaxNodesNeeded.sh $config_file $cores)
#Run with the expected amount of nodes
sbatch -p $partition -N $node_qty -t $limit_time $PROTEO_HOME$execDir/generalRun.sh $config_file $use_external $outFileIndex $qty

if ! [ -z "$output" ]
then
  mkdir -p $output
  echo "Moving data to $output\nMoved files:"
  ls R${outFileIndex}_G*
  mv R${outFileIndex}_G* $output
  if [ "$use_external" -eq 2 ] # Extrae additional output
  then
    mv a.out.* $output
    mv TRACE* $output
    mv set-0/ $output
  elif [ "$use_external" -eq 1 ] # Valgrind additional output
  then
    mv vg.* $output
  fi
fi
