#!/bin/bash

#SBATCH --exclude=c02,c01,c00
#SBATCH -p P1

# !!!!This script should only be called by others scripts, do not call it directly!!!
# Runs a given configuration file with the indicated parameters with the aid of the RMS Slurm.
# Parameter 1 - Base directory of the malleability benchmark
# Parameter 2 - Number of cores in a single machine
# Parameter 3 - Configuration file name for the emulation.
# Parameter 4 - Use Valgrind(1), Extrae(2) or nothing(0).
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
#$4 == use_external
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
use_external=$4
outFileIndex=$5
qty=1
if [ $# -ge 5 ]
then
  qty=$6
fi

nodelist=$SLURM_JOB_NODELIST
if [ -z "$nodelist" ];
then
  echo "Internal ERROR in generalRun.sh - Nodelist not provided"
  exit -1
fi

numP=$(bash $dir$execDir/BashScripts/getNumPNeeded.sh $configFile 0)
initial_nodelist=$(bash $dir$execDir/BashScripts/createInitialNodelist.sh $numP $cores $nodelist)

#EXECUTE RUN
echo "Nodes=$nodelist"
if [ $use_external -eq 0 ] #NORMAL
then
  for ((i=0; i<qty; i++))
  do
    echo "Run $i starts"
    mpirun -hosts $initial_nodelist -np $numP $dir$codeDir/a.out $configFile $outFileIndex
    echo "Run $i ends"
  done
elif [ $use_external -eq 1 ] #VALGRIND
then
  cp $dir$execDir/Valgrind/worker_valgrind.sh .
  for ((i=0; i<qty; i++))
  do
    echo "Run $i starts"
    mpirun -hosts $initial_nodelist -np $numP valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --trace-children=yes --log-file=vg.sp.%p.$SLURM_JOB_ID.$i $dir$codeDir/a.out $configFile $outIndex 
    echo "Run $i ends"
  done
else #EXTRAE
  cp $dir$execDir/Extrae/extrae.xml .
  cp $dir$execDir/Extrae/trace.sh .
  cp $dir$execDir/Extrae/worker_extrae.sh .
  for ((i=0; i<qty; i++))
  do
    #FIXME Extrae not tested keeping in mind the initial nodelist - Could have some errors
    srun -n$numP --mpi=pmi2 ./trace.sh $dir$codeDir/a.out $configFile $outFileIndex
  done
fi

echo "END TEST"
sed -i 's/application called MPI_Abort(MPI_COMM_WORLD, -100) - process/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
sed -i 's/Abort(-100)/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
