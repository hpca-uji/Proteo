#!/bin/bash

#SBATCH -p P1
#SBATCH -N 1
#SBATCH --exclude=c01,c00,c02

dir="/home/martini/malleability_benchmark"
codeDir="/Codes"

nodelist="localhost"
nodes=1
configFile=$1
outIndex=$2

echo "MPICH"
#module load mpich-3.4.1-noucx
#export HYDRA_DEBUG=1

numP=$(bash $dir$execDir/BashScripts/getNumPNeeded.sh $configFile 0)
mpirun -np $numP valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --trace-children=yes --log-file=nc.vg.%p $dir$codeDir/build/a.out $configFile $outIndex $nodelist $nodes

echo "END RUN"
sed -i 's/application called MPI_Abort(MPI_COMM_WORLD, -100) - process/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
sed -i 's/Abort(-100)/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
