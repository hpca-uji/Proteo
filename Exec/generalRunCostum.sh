#!/bin/bash

# !!!!This script should only be called by others scripts, do not call it directly!!!
# Runs a given configuration file with the indicated parameters.
# Parameter 1 - Base directory of the malleability benchmark
# Parameter 2 - Number of cores in a single machine
# Parameter 3 - Configuration file name for the emulation.
# Parameter 4 - Use Extrae(1) or not(0).
# Parameter 5 - Index to use for the output files. Must be a positive integer.
# Parameter 6 - Amount of executions per file. Must be a positive number.
#====== Do not modify these values =======

codeDir="/Codes/build"
execDir="/Exec"
ResultsDir="/Results"

echo "START TEST"

#$1 == baseDir
#$2 == cores
#$3 == configFile
#$4 == use_extrae
#$5 == outFileIndex
#$6 == qty

echo $@
if [ $# -lt 3 ]
then
  echo "Internal ERROR generalRunCostum.sh - Not enough arguments were given"
  exit -1
fi

#READ PARAMETERS AND ENSURE CORRECTNESS
dir=$1
cores=$2
configFile=$3
use_extrae=0
outFileIndex=0
qty=1

if [ $# -ge 4 ]
then
  use_extrae=$3
fi

if [ $# -ge 5 ]
then
  outFileIndex=$4
fi

if [ $# -ge 6 ]
then
  qty=$5
fi

numP=$(bash $dir$execDir/BashScripts/getNumPNeeded.sh $configFile 0)
nodelist=$SLURM_JOB_NODELIST
nodes=$SLURM_JOB_NUM_NODES
if [ -z "$nodelist" ];
then
  nodelist="localhost"
  initial_nodelist="localhost"
else
  initial_nodelist=$(bash $dir$execDir/BashScripts/createInitialNodelist.sh $numP $cores $nodelist)
fi
if [ -z "$nodes" ];
then
  nodes=1
fi

#EXECUTE RUN
echo "Nodes=$nodelist"
if [ $use_extrae -ne 1 ]
then
  for ((i=0; i<qty; i++))
  do
    mpirun -hosts $initial_nodelist -np $numP $dir$codeDir/a.out $configFile $outFileIndex $nodelist $nodes 
  done
else
  cp $dir$execDir/Extrae/extrae.xml .
  cp $dir$execDir/Extrae/trace.sh .
  cp $dir$execDir/Extrae/trace_worker.sh .
  for ((i=0; i<qty; i++))
  do
    mpirun -hosts $initial_nodelist -np $numP ./trace.sh $dir$codeDir/a.out $configFile $outFileIndex $nodelist $nodes 
  done
fi

echo "END TEST"
