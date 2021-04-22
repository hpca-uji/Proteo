#!/bin/bash

#SBATCH -N 2

#module load gcc/6.4.0
#module load openmpi/1.10.7

#module load /home/martini/ejemplos_Mod/modulefiles/mpi/openmpi_aliaga_1_10_7


echo "OPENMPI"
#module load /home/martini/MODULES/modulefiles/openmpi4.1.0

#mpirun -mca btl_openib_allow_ib 1 -npernode 10 -np 20 ./batch5.out

echo "MPICH"
module load mpich-3.4.1-noucx
#module load /home/martini/MODULES/modulefiles/mpich3.4
#export HYDRA_DEBUG=1
#-disable-hostname-propagation -disable-auto-cleanup -pmi-port -hosts n00,n01

mpirun -ppn 1 -np 2 ./a.out test.ini


echo "Intel"
#module load /home/martini/MODULES/modulefiles/intel64.module

#export I_MPI_OFI_PROVIDER=tcp
#export I_MPI_DEBUG=6
#export I_MPI_FABRICS=shm:ofi

#mpirun -print-all-exitcodes -np 2 --ppn 1 ./batch.out
#mpirun -genv I_MPI_FABRICS=shm:ofi -print-all-exitcodes -iface ib0 -np 16 ./batch4.out
#mpirun -genv I_MPI_FABRICS=shm:ofi -iface enp59s0f0 -np 20 ./batch4.out
