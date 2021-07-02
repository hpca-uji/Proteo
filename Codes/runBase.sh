#!/bin/bash

#SBATCH -N 1

echo "MPICH"
module load mpich-3.4.1-noucx
#export HYDRA_DEBUG=1
#-disable-hostname-propagation -disable-auto-cleanup -pmi-port -hosts n00,n01

numP=$(bash recordMachinefile.sh $1)

mpirun -f hostfile.o$SLURM_JOB_ID -np $numP ./a.out $1 $2
#mpirun -np 2 ./a.out test.ini
rm hostfile.o$SLURM_JOB_ID


echo "END RUN"
