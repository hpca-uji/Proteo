module load mpich-3.4.1-noucx

mpicc -Wall Main/Main.c IOcodes/read_ini.c IOcodes/ini.c malleability/ProcessDist.c malleability/CommDist.c -pthread -lslurm
