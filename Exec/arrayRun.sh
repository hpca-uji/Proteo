#!/bin/bash

#SBATCH --exclude=c01,c00

dir="/home/martini/malleability_benchmark"
codeDir="/Codes"
ResultsDir="/Results"
module load mpich-3.4.1-noucx

name_dir=$1
i=$2
procs_parents=$3
procs_sons=$4
#percs_array=(0 25 50 75 100)
percs_array=(0)

aux=$(($i + 1))
echo "START TEST init=$aux"
for adr_perc in "${percs_array[@]}"
do

  for phy_dist in cpu node
  do

    for ibarrier_use in 3 #TODO Simplificar
    do
      i=$(($i + 1))
      cd $name_dir/Run$i
      config_file="config$i.ini"

      echo "EXEC $procs_parents -- $procs_sons -- $adr_perc -- $ibarrier_use -- $phy_dist -- RUN $i"

      for index in 1 2 3
      do
        numP=$(bash $dir$codeDir/recordMachinefile.sh $config_file) # Crea el fichero hostfile
        mpirun -f hostfile.o$SLURM_JOB_ID $dir$codeDir/./bench.out $config_file $i
        rm hostfile.o$SLURM_JOB_ID
      done
    done  
  done
done
echo "END TEST"
