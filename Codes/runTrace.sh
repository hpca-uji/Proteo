#!/bin/bash

#SBATCH -p P1
#SBATCH -N 1
#SBATCH --exclude=c01,c00,c02

scriptDir="$(dirname "$0")"
source $scriptDir/build/config.txt
resultsDir="/Results"

nodelist=$SLURM_JOB_NODELIST
nodes=$SLURM_JOB_NUM_NODES
configFile=$1
outIndex=$2

echo "MPICH"

numP=$(bash $PROTEO_HOME$execDir/BashScripts/getNumPNeeded.sh $configFile 0)

name_res="Extrae_"$nodes"_Test_"$numP
dir_name_res=$PROTEO_HOME$resultsDir"/"$name_res

#mpirun -np $numP $PROTEO_BIN $configFile $outIndex $nodelist $nodes
srun -n$numP --mpi=pmi2 ./trace.sh $PROTEO_BIN $configFile $outIndex $nodelist $nodes

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
