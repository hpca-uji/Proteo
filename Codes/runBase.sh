#!/bin/bash

#SBATCH -N 1

echo "MPICH"
module load mpich-3.4.1-noucx
#export HYDRA_DEBUG=1
#-disable-hostname-propagation -disable-auto-cleanup -pmi-port -hosts n00,n01

numP=$(bash recordMachinefile.sh test.ini)

#mpirun -f hostfile.o$SLURM_JOB_ID -np $numP ./a.out test.ini
mpirun -np 2 ./a.out test.ini
rm hostfile.o$SLURM_JOB_ID


echo "END RUN"
