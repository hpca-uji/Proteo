#!/bin/bash

partition="P1"
exclude="c00,c01,c02"

# Executes a given configuration file with the aid of
# the RMS Slurm.
# Parameter 1: Configuration file name for the emulation.
# Parameter 2(Optional): Index to use for the output files. Must be a positive integer.
# Parameter 3(Optional): Number of repetitions to perform. Must be a positive integer.
# Parameter 4(Optional): Use Valgrind(1), Extrae(2) or nothing(0).
# Parameter 5(Optional): Maximum amount of time in seconds needed by a single execution. Default value is 0, which indicates infinite time. Must be a positive integer.
# Parameter 6(Optional): Path where the output files should be saved. 
#====== Do not modify these values =======

scriptDir="$(dirname "$0")"
source $scriptDir/../Codes/build/config.txt
cores=$(bash $PROTEO_HOME$execDir/BashScripts/getCores.sh $partition)

if [ $# -lt 1 ]
then
  echo "Not enough arguments. Usage:"
  echo "bash singleRun.sh config.ini [outFileIndex] [Qty] [Use extrae] [Output path]"
  exit 1
fi

#$1 == configFile
#$2 == outFileIndex
#$3 == Qty of repetitions
#$4 == Use external NO(0) Valgrind(1), Extrae(2)
#$5 == Max time per execution(s)
#$6 == Output path

config_file=$1
outFileIndex=0
qty=1
use_external=0

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
  use_external=$4
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
node_qty=$(bash $PROTEO_HOME$execDir/BashScripts/getMaxNodesNeeded.sh $config_file $cores)
#Run with the expected amount of nodes
sbatch -p $partition --exclude=$exclude -N $node_qty -t $limit_time $PROTEO_HOME$execDir/generalRun.sh $cores $config_file $use_external $outFileIndex $qty

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
