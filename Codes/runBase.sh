#!/bin/bash

#SBATCH -p P1
#SBATCH -N 1
#SBATCH --mem-per-cpu=6000
#SBATCH --exclusive

source build/config.txt
configFile=$1

outIndex=0
if [ $# -ge 2 ]
then
  outIndex=$2 
fi

module list
echo "MPICH provider=$FI_PROVIDER"
mpirun --version
numP=$(bash $PROTEO_HOME$execDir/BashScripts/getNumPNeeded.sh $configFile 0)
initial_nodelist=$(bash $PROTEO_HOME$execDir/BashScripts/createInitialNodelist.sh $numP)
echo $initial_nodelist
echo "Test PreRUN $numP $SLURM_JOB_NODELIST"
mpirun -hosts $initial_nodelist -np $numP $PROTEO_BIN $configFile $outIndex 

echo "END RUN"
sed -i 's/application called MPI_Abort(MPI_COMM_WORLD, -100) - process/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
sed -i 's/Abort(-100)/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
MAM_ID=$(($SLURM_JOB_ID % 1000))
rm MAM_HF_ID*$MAM_ID*S*.tmp
