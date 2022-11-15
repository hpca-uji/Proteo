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

aux=$(grep "\[resize0\]" -n $configFile | cut -d ":" -f1)
read -r ini fin <<<$(echo $aux)
diff=$(( fin - ini ))
numP=$(head -$fin $configFile | tail -$diff | cut -d ';' -f1 | grep Procs | cut -d '=' -f2)

mpirun -np $numP $dir$codeDir/build/a.out $configFile $outIndex $nodelist $nodes

echo "END RUN"
sed -i 's/application called MPI_Abort(MPI_COMM_WORLD, -100) - process/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
