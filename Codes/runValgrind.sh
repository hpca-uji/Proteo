#!/bin/bash

#SBATCH -p P1
#SBATCH -N 1
#SBATCH --exclude=c01,c00,c02

source build/config.txt
cores=$(bash $PROTEO_HOME$execDir/BashScripts/getCores.sh $partition)

nodelist=$SLURM_JOB_NODELIST
nodes=$SLURM_JOB_NUM_NODES
configFile=$1

outIndex=0
if [ $# -ge 2 ]
then
  outIndex=$2
fi

echo "MPICH provider=$FI_PROVIDER"
mpirun --version
numP=$(bash $PROTEO_HOME$execDir/BashScripts/getNumPNeeded.sh $configFile 0)
initial_nodelist=$(bash $PROTEO_HOME$execDir/BashScripts/createInitialNodelist.sh $numP $cores $nodelist)
echo $initial_nodelist
echo "Test PreRUN $numP $nodelist"
mpirun -hosts $initial_nodelist -np $numP valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --trace-children=yes --log-file=vg.sp.%p.$SLURM_JOB_ID $PROTEO_BIN $configFile $outIndex 

echo "END RUN"
sed -i 's/application called MPI_Abort(MPI_COMM_WORLD, -100) - process/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
sed -i 's/Abort(-100)/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
