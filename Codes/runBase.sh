#!/bin/bash

#SBATCH -N 1
#SBATCH --exclude=c01,c00,c02

dir="/home/martini/malleability_benchmark"
codeDir="/Codes"

echo "MPICH"
module load mpich-3.4.1-noucx
#export HYDRA_DEBUG=1
#-disable-hostname-propagation -disable-auto-cleanup -pmi-port -hosts n00,n01

numP=$(bash recordMachinefile.sh $1)

#mpirun -f hostfile.o$SLURM_JOB_ID ./a.out $1 $2
mpirun -print-all-exitcodes -f hostfile.o$SLURM_JOB_ID $dir$codeDir/a.out $1 $2
rm hostfile.o$SLURM_JOB_ID

echo "END RUN"
sed -i 's/application called MPI_Abort(MPI_COMM_WORLD, -100) - process/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
