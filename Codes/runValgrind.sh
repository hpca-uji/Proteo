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

aux=$(grep "\[resize0\]" -n $1 | cut -d ":" -f1)
read -r ini fin <<<$(echo $aux)
diff=$(( fin - ini ))
numP=$(head -$fin $1 | tail -$diff | cut -d ';' -f1 | grep Procs | cut -d '=' -f2)

mpirun -np $numP valgrind --leak-check=full --show-leak-kinds=all --trace-children=yes --log-file=nc.vg.%p $dir$codeDir/build/a.out $configFile $outIndex $nodelist $nodes

echo "END RUN"
sed -i 's/application called MPI_Abort(MPI_COMM_WORLD, -100) - process/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
