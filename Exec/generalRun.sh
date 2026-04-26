#!/bin/bash

#SBATCH --mem-per-cpu=6000
#SBATCH --exclusive
#SBATCH --exclude=c02,c01,c00
#SBATCH -p P1

# !!!!This script should only be called by others scripts, do not call it directly!!!
# Runs a given configuration file with the indicated parameters with the aid of the RMS Slurm.
# Parameter 1 - Configuration file name for the emulation.
# Parameter 2 - Use Valgrind(1), Extrae(2) or nothing(0).
# Parameter 3 - Index to use for the output files. Must be a positive integer.
# Parameter 4 - Amount of executions per file. Must be a positive number.
#====== Do not modify these values =======

execDir="/Exec"

echo "START TEST P=$SLURM_JOB_PARTITION"

#$1 == configFile
#$2 == use_external
#$3 == outFileIndex
#$4 == qty

echo $@
if [ $# -lt 2 ]
then
  echo "Internal ERROR generalRun.sh - Not enough arguments were given"
  exit -1
fi

#READ PARAMETERS AND ENSURE CORRECTNESS
configFile=$1
use_external=$2
outFileIndex=$3
qty=1
if [ $# -ge 3 ]
then
  qty=$4
fi

if [ -z "$SLURM_JOB_NODELIST" ];
then
  echo "Internal ERROR in generalRun.sh - Nodelist not provided"
  exit -1
fi

numP=$(bash $PROTEO_HOME$execDir/BashScripts/getNumPNeeded.sh $configFile 0)
initial_nodelist=$(bash $PROTEO_HOME$execDir/BashScripts/createInitialNodelist.sh $numP)
ln -sf $PROTEO_HOME$execDir/SAM_R_FILE.tmp SAM_R_FILE.tmp

#EXECUTE RUN
which mpirun
echo "Nodes=$SLURM_JOB_NODELIST - Starting hostlist=$initial_nodelist"
if [ $use_external -eq 0 ] #NORMAL
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
else #EXTRAE
  cp $PROTEO_HOME$execDir/Extrae/extrae.xml .
  cp $PROTEO_HOME$execDir/Extrae/trace.sh .
  cp $PROTEO_HOME$execDir/Extrae/worker_extrae.sh .
  for ((i=0; i<qty; i++))
  do
    #FIXME Extrae not tested keeping in mind the initial nodelist - Could have some errors
    srun -n$numP --mpi=pmi2 ./trace.sh $PROTEO_BIN $configFile $outFileIndex
  done
fi

echo "END TEST"
sed -i 's/application called MPI_Abort(MPI_COMM_WORLD, -100) - process/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
sed -i 's/Abort(-100)/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
rm MAM_HF_ID${SLURM_JOB_ID}_S*.tmp
rm SAM_W_J${SLURM_JOB_ID}_S*_ID*.tmp
