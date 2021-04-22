#!/bin/bash

#SBATCH -N 2

echo "MPICH"
module load mpich-3.4.1-noucx
#module load /home/martini/MODULES/modulefiles/mpich3.4
#export HYDRA_DEBUG=1
#-disable-hostname-propagation -disable-auto-cleanup -pmi-port -hosts n00,n01

mpirun -ppn 1 -np 2 ./a.out test.ini

echo "END RUN"
