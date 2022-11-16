#!/bin/bash

#SBATCH --exclude=c02,c01,c00
#SBATCH -p P1

dir="/home/martini/malleability_benchmark"
codeDir="/Codes/build"

nodelist=$SLURM_JOB_NODELIST
nodes=$SLURM_JOB_NUM_NODES

if [ $# -lt 1 ]
then
  echo "Not enough arguments. Usage:"
  echo "singleRun.sh config.ini [outFileIndex] [Qty] [Output path]"
  exit 1
fi

echo "START TEST"

#$1 == configFile
#$2 == outFileIndex
#$3 == Qty of repetitions
#$4 == Output path

configFile=$1
outFileIndex=$2
qty=1

if [ $# -gt 2 ]
then
  qty=$3
  if [ $# -gt 3 ]
  then
    output=$4
  fi
fi

aux=$(grep "\[resize0\]" -n $configFile | cut -d ":" -f1)
read -r ini fin <<<$(echo $aux)
diff=$(( fin - ini ))
numP=$(head -$fin $configFile | tail -$diff | cut -d ';' -f1 | grep Procs | cut -d '=' -f2)

echo "Nodes=$SLURM_JOB_NODELIST"
for ((i=0; i<qty; i++))
do
  echo "Iter $i -- numP=$numP"
  mpirun -np $numP $dir$codeDir/a.out $configFile $outFileIndex $nodelist $nodes 
done

echo "END TEST"
sed -i 's/application called MPI_Abort(MPI_COMM_WORLD, -100) - process/shrink cleaning/g' slurm-$SLURM_JOB_ID.out

if [ $# -gt 3 ]
then
  echo "Moving data to $output\nMoved files:"
  ls R${outFileIndex}_G*
  mv R${outFileIndex}_G* $output
fi
