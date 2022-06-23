
#mpicc -Wall Main/Main.c Main/computing_func.c IOcodes/results.c IOcodes/read_ini.c IOcodes/ini.c malleability/ProcessDist.c malleability/CommDist.c -pthread -lslurm -lm
mpicc -g -Wall Main/Main.c Main/computing_func.c Main/comunication_func.c Main/linear_reg.c Main/process_stage.c IOcodes/results.c IOcodes/read_ini.c IOcodes/ini.c malleability/malleabilityManager.c malleability/malleabilityTypes.c malleability/malleabilityZombies.c malleability/ProcessDist.c malleability/CommDist.c -pthread -lslurm -lm

if [ $# -gt 0 ]
then
  if [ $1 = "-e" ]
  then
    echo "Creado ejecutable para ejecuciones"
    cp a.out bench.out
  fi
fi
