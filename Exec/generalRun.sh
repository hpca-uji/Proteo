#!/bin/bash

#SBATCH --exclude=c02,c01,c00
#SBATCH -p P1

# !!!!This script should only be called by others scripts, do not call it directly!!!
# Runs a given configuration file with the indicated parameters with the aid of the RMS Slurm.
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
#$1 == cores
#$3 == configFile
#$4 == use_extrae
#$5 == outFileIndex
#$6 == qty

echo $@
if [ $# -lt 4 ]
then
  echo "Internal ERROR generalRun.sh - Not enough arguments were given"
  exit -1
fi

#READ PARAMETERS AND ENSURE CORRECTNESS
dir=$1
cores=$2
configFile=$3
use_extrae=$4
outFileIndex=$5
qty=1
if [ $# -ge 5 ]
then
  qty=$6
fi

nodelist=$SLURM_JOB_NODELIST
nodes=$SLURM_JOB_NUM_NODES
if [ -z "$nodelist" ];
then
  echo "Internal ERROR in generalRun.sh - Nodelist not provided"
  exit -1
fi
if [ -z "$nodes" ];
then
  nodes=1
fi

numP=$(bash $dir$execDir/BashScripts/getNumPNeeded.sh $configFile 0)
initial_nodelist=$(bash $dir$execDir/BashScripts/createInitialNodelist.sh $numP $cores $nodelist)

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
    #FIXME Extrae not tested keeping in mind the initial nodelist - Could have some errors
    srun -n$numP --mpi=pmi2 ./trace.sh $dir$codeDir/a.out $configFile $outFileIndex $nodelist $nodes
  done
fi

echo "END TEST"
sed -i 's/application called MPI_Abort(MPI_COMM_WORLD, -100) - process/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
sed -i 's/Abort(-100)/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
