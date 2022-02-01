#!/bin/bash

#SBATCH --exclude=c02,c01,c00

dir="/home/martini/malleability_benchmark"
codeDir="/Codes"
ResultsDir="/Results"

module load mpich-3.4.1-noucx
echo "START TEST"

#$1 == configFile
#$2 == outFileIndex
#$3 == cantidad de ejecuciones

if [ $# -gt 2 ]
then
  qty=$3
else
  qty=1
fi

for ((i=0; i<qty; i++))
do
  echo "Iter $i"
  numP=$(bash $dir$codeDir/recordMachinefile.sh $1)
  mpirun -f hostfile.o$SLURM_JOB_ID $dir$codeDir/bench.out $1 $2
  rm hostfile.o$SLURM_JOB_ID
done

echo "END TEST"
sed -i 's/application called MPI_Abort(MPI_COMM_WORLD, -100) - process/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
