#!/bin/bash
#This script should only be called by others scripts, do not call it directly

#SBATCH --exclude=c02,c01,c00
#SBATCH -p P1
codeDir="/Codes/build"

echo "START TEST"

#$1 == baseDir
#$2 == configFile
#$3 == use_extrae
#$4 == outFileIndex

echo $@
if [ $# -lt 3 ]
then
  echo "Internal ERROR generalRun.sh - Not enough arguments were given"
  exit -1
fi

dir=$1
configFile=$2
use_extrae=$3
outFileIndex=$4

aux=$(grep "\[resize0\]" -n $configFile | cut -d ":" -f1)
read -r ini fin <<<$(echo $aux)
diff=$(( fin - ini ))
numP=$(head -$fin $configFile | tail -$diff | cut -d ';' -f1 | grep Procs | cut -d '=' -f2)

echo "Nodes=$SLURM_JOB_NODELIST"
if [ $use_extrae -ne 1 ]
then
  mpirun -np $numP $dir$codeDir/a.out $configFile $outFileIndex $SLURM_JOB_NODELIST $SLURM_JOB_NUM_NODES 
else
  srun -n$numP --mpi=pmi2 ./trace.sh $dir$codeDir/a.out $configFile $outFileIndex $SLURM_JOB_NODELIST $SLURM_JOB_NUM_NODES
fi


echo "END TEST"
sed -i 's/application called MPI_Abort(MPI_COMM_WORLD, -100) - process/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
