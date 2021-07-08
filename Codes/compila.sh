module load mpich-3.4.1-noucx

mpicc -Wall Main/Main.c Main/computing_func.c IOcodes/results.c IOcodes/read_ini.c IOcodes/ini.c malleability/ProcessDist.c malleability/CommDist.c -pthread -lslurm -lm

if [ $# -gt 0 ]
then
  if [ $1 = "-e" ]
  then
    cp a.out bench.out
  fi
fi
