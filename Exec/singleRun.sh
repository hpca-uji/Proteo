#!/bin/bash

dir="/home/martini/malleability_benchmark"
codeDir="/Codes"
ResultsDir="/Results"

module load mpich-3.4.1-noucx
echo "START TEST"

numP=$(bash $dir$codeDir/recordMachinefile.sh $1)
mpirun -f hostfile.o$SLURM_JOB_ID -np $numP $dir$codeDir/a.out $1 $2
rm hostfile.o$SLURM_JOB_ID

echo "END TEST"
