#!/bin/bash

#SBATCH -p P1
#SBATCH -N 1
#SBATCH --exclude=c01,c00,c02

partition='P1'

scriptDir="$(dirname "$0")"
source $scriptDir/build/config.txt
codeDir="/Codes"
execDir="/Exec"
cores=$(bash $dir$execDir/BashScripts/getCores.sh $partition)

nodelist=$SLURM_JOB_NODELIST
nodes=$SLURM_JOB_NUM_NODES
configFile=$1

outIndex=0
if [ $# -ge 2 ]
then
  outIndex=$2
fi

echo "MPICH"
#export HYDRA_DEBUG=1

numP=$(bash $dir$execDir/BashScripts/getNumPNeeded.sh $configFile 0)
initial_nodelist=$(bash $dir$execDir/BashScripts/createInitialNodelist.sh $numP $cores $nodelist)
echo $initial_nodelist
echo "Test PreRUN $numP $nodelist"
mpirun -hosts $initial_nodelist -np $numP $dir$codeDir/build/a.out $configFile $outIndex $nodelist $nodes

echo "END RUN"
sed -i 's/application called MPI_Abort(MPI_COMM_WORLD, -100) - process/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
sed -i 's/Abort(-100)/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
