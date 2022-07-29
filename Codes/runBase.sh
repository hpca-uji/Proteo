#!/bin/bash

#SBATCH -p P1
#SBATCH -N 1
#SBATCH --exclude=c01,c00,c02

dir="/home/martini/malleability_benchmark"
codeDir="/Codes"

nodelist=$SLURM_JOB_NODELIST
nodes=$SLURM_JOB_NUM_NODES
configFile=$1
outIndex=$2

echo "MPICH"
#module load mpich-3.4.1-noucx
#export HYDRA_DEBUG=1

numP=$(bash recordMachinefile.sh $configFile)

#mpirun -np 4 /home/martini/Instalaciones/valgrind-mpich-3.4.1-noucx/bin/valgrind --leak-check=full --show-leak-kinds=all --log-file=nc.vg.%p $dir$codeDir/a.out $configFile $outIndex $nodelist $nodes
mpirun -np $numP $dir$codeDir/exec/a.out $configFile $outIndex $nodelist $nodes
rm hostfile.o$SLURM_JOB_ID

echo "END RUN"
sed -i 's/application called MPI_Abort(MPI_COMM_WORLD, -100) - process/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
