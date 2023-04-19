#!/bin/bash

#SBATCH -p P1
#SBATCH -N 1
#SBATCH --exclude=c01,c00,c02

dir="/home/martini/malleability_benchmark"
codeDir="/Codes/build"
resultsDir="/Results"
execDir="/Exec"

nodelist=$SLURM_JOB_NODELIST
nodes=$SLURM_JOB_NUM_NODES
configFile=$1
outIndex=$2

echo "MPICH"

numP=$(bash $dir$execDir/BashScripts/getNumPNeeded.sh $configFile 0)

name_res="Extrae_"$nodes"_Test_"$numP
dir_name_res=$dir$resultsDir"/"$name_res

#mpirun -np $numP $dir$codeDir/a.out $configFile $outIndex $nodelist $nodes
srun -n$numP --mpi=pmi2 ./trace.sh $dir$codeDir/a.out $configFile $outIndex $nodelist $nodes

echo "END RUN"
sed -i 's/application called MPI_Abort(MPI_COMM_WORLD, -100) - process/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
sed -i 's/Abort(-100)/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
rm hostfile.o$SLURM_JOB_ID

echo "MOVING DATA"
mkdir $dir_name_res
mv a.out.* $dir_name_res
mv TRACE* $dir_name_res
mv set-0/ $dir_name_res
mv R$outIndex* $dir_name_res
echo "JOB ENDED"
