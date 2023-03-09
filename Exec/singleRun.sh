#!/bin/bash

dir="/home/martini/malleability_benchmark"
partition="P1"
exclude="c00,c01,c02"
cores=20

# Executes a given configuration file with the aid of
# the RMS Slurm.
# Parameter 1: Configuration file name for the emulation.
# Parameter 2(Optional): Index to use for the output files. Must be a positive integer.
# Parameter 3(Optional): Number of repetitions to perform. Must be a positive integer.
# Parameter 4(Optional): Use Extrae(1) or not(0).
# Parameter 5(Optional): Path where the output files should be saved. 
#====== Do not modify these values =======

codeDir="/Codes/build"
execDir="/Exec"
ResultsDir="/Results"

if [ $# -lt 2 ]
then
  echo "Not enough arguments. Usage:"
  echo "bash singleRun.sh config.ini [outFileIndex] [Qty] [Use extrae] [Output path]"
  exit 1
fi

#$1 == configFile
#$2 == outFileIndex
#$3 == Qty of repetitions
#$4 == Use extrae NO(0) YES(1)
#$5 == Output path

config_file=$1
outFileIndex=0
qty=1
use_extrae=0

if [ $# -gt 2 ]
then
  outFileIndex=$2
fi
if [ $# -gt 3 ]
then
  qty=$3
fi
if [ $# -gt 4 ]
then
  use_extrae=$4
fi
if [ $# -gt 5 ]
then
  output=$5
fi

#1 - Obtain maximum number of processes for the run
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

#2 - Obtain needed nodes for the number of processes
node_qty=$(($max_numP / $cores))
if [ "$node_qty" -eq "0" ];
then
  node_qty=1
fi

#3 - Run with the expected amount of nodes
sbatch -p $partition --exclude=$exclude -N $node_qty $dir$execDir/generalRun.sh $dir $config_file $use_extrae $outFileIndex $qty

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
