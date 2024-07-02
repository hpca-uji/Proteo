#!/bin/bash

# !!!!This script should only be called by others scripts, do not call it directly!!!
# Runs a given configuration file with the indicated parameters.
# Parameter 1 - Number of cores in a single machine
# Parameter 2 - Configuration file name for the emulation.
# Parameter 3 - Use Extrae(1) or not(0).
# Parameter 4 - Index to use for the output files. Must be a positive integer.
# Parameter 5 - Amount of executions per file. Must be a positive number.
#====== Do not modify these values =======

execDir="/Exec"

echo "START TEST"

#$1 == cores
#$2 == configFile
#$3 == use_external
#$4 == outFileIndex
#$5 == qty

echo $@
if [ $# -lt 3 ]
then
  echo "Internal ERROR generalRunCostum.sh - Not enough arguments were given"
  exit -1
fi

#READ PARAMETERS AND ENSURE CORRECTNESS
cores=$1
configFile=$2
use_external=0
outFileIndex=0
qty=1

if [ $# -ge 3 ]
then
  use_external=$3
fi

if [ $# -ge 4 ]
then
  outFileIndex=$4
fi

if [ $# -ge 5 ]
then
  qty=$5
fi

numP=$(bash $PROTEO_HOME$execDir/BashScripts/getNumPNeeded.sh $configFile 0)
nodelist=$SLURM_JOB_NODELIST
if [ -z "$nodelist" ];
then
  nodelist="localhost"
  initial_nodelist="localhost"
else
  initial_nodelist=$(bash $PROTEO_HOME$execDir/BashScripts/createInitialNodelist.sh $numP $cores $nodelist)
fi

#EXECUTE RUN
echo "Nodes=$nodelist"
if [ $use_external -eq 0 ]
then
  for ((i=0; i<qty; i++))
  do
    echo "Run $i starts"
    mpirun -hosts $initial_nodelist -np $numP $PROTEO_BIN $configFile $outFileIndex
    echo "Run $i ends"
  done
elif [ $use_external -eq 1 ] #VALGRIND
then
  cp $PROTEO_HOME$execDir/Valgrind/worker_valgrind.sh .
  for ((i=0; i<qty; i++))
  do
    echo "Run $i starts"
    mpirun -hosts $initial_nodelist -np $numP valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --trace-children=yes --log-file=vg.sp.%p.$SLURM_JOB_ID.$i $PROTEO_BIN $configFile $outIndex 
    echo "Run $i ends"
  done
else
  cp $PROTEO_HOME$execDir/Extrae/extrae.xml .
  cp $PROTEO_HOME$execDir/Extrae/trace.sh .
  cp $PROTEO_HOME$execDir/Extrae/worker_extrae.sh .
  for ((i=0; i<qty; i++))
  do
    mpirun -hosts $initial_nodelist -np $numP ./trace.sh $PROTEO_BIN $configFile $outFileIndex 
  done
fi

echo "END TEST"
