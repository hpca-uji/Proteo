#!/bin/bash
#This script should only be called by others scripts, do not call it directly

#SBATCH --exclude=c02,c01,c00
#SBATCH -p P1
codeDir="/Codes/build"
execDir="/Exec"
ResultsDir="/Results"

echo "START TEST"

#$1 == baseDir
#$2 == configFile
#$3 == use_extrae
#$4 == outFileIndex
#$5 == qty

echo $@
if [ $# -lt 3 ]
then
  echo "Internal ERROR generalRun.sh - Not enough arguments were given"
  exit -1
fi

#READ PARAMETERS AND ENSURE CORRECTNESS
dir=$1
configFile=$2
use_extrae=$3
outFileIndex=$4
qty=1
if [ $# -ge 5 ]
then
  qty=$5
fi

nodelist=$SLURM_JOB_NODELIST
nodes=$SLURM_JOB_NUM_NODES
if [ -z "$nodelist" ];
then
  echo "Internal ERROR in generalRun.sh - Nodelist not provided"
  exit -1
fi
if [ -z "$nodes" ];
then
  nodes=1
fi

aux=$(grep "\[resize0\]" -n $configFile | cut -d ":" -f1)
read -r ini fin <<<$(echo $aux)
diff=$(( fin - ini ))
numP=$(head -$fin $configFile | tail -$diff | cut -d ';' -f1 | grep Procs | cut -d '=' -f2)

#EXECUTE RUN
echo "Nodes=$nodelist"
if [ $use_extrae -ne 1 ]
then
  for ((i=0; i<qty; i++))
  do
    mpirun -np $numP $dir$codeDir/a.out $configFile $outFileIndex $nodelist $nodes 
  done
else
  cp $dir$execDir/Extrae/extrae.xml .
  cp $dir$execDir/Extrae/trace.sh .
  cp $dir$execDir/Extrae/trace_worker.sh .
  for ((i=0; i<qty; i++))
  do
    srun -n$numP --mpi=pmi2 ./trace.sh $dir$codeDir/a.out $configFile $outFileIndex $nodelist $nodes
  done
fi

echo "END TEST"
sed -i 's/application called MPI_Abort(MPI_COMM_WORLD, -100) - process/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
sed -i 's/Abort(-100)/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
