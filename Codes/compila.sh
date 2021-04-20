module load /home/martini/MODULES/modulefiles/mpich3.4

mpicc -Wall Main.c ../IOcodes/read_ini.c ../IOcodes/ini.c ../malleability/ProcessDist.c ../malleability/CommDist.c -pthread -lslurm
