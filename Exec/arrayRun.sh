#!/bin/bash

#SBATCH --exclude=c02,c01,c00
#SBATCH -p P1

dir="/home/martini/malleability_benchmark"
codeDir="/Codes"
ResultsDir="/Results"

nodelist=$SLURM_JOB_NODELIST
nodes=$SLURM_JOB_NUM_NODES

module load mpich-3.4.1-noucx

name_dir=$1
i=$2
procs_parents=$3
procs_sons=$4
#percs_array=(0 25 50 75 100)
percs_array=(0)
at_array=(0)
dist_array=(cpu)
cst_array=(0 1 2 3)
css_array=(0 1)

aux=$(($i + 1))
echo "START TEST init=$aux"
for adr_perc in "${percs_array[@]}"
do
  for phy_dist in "${dist_array[@]}"
  do
    for ibarrier_use in "${at_array[@]}"
    do
      for cst in "${cst_array[@]}"
      do
        for css in "${css_array[@]}"
        do

          i=$(($i + 1))
          cd $name_dir/Run$i
          configFile="config$i.ini"

          aux=$(grep "\[resize0\]" -n $configFile | cut -d ":" -f1)
          read -r ini fin <<<$(echo $aux)
          diff=$(( fin - ini ))
          numP=$(head -$fin $configFile | tail -$diff | cut -d ';' -f1 | grep Procs | cut -d '=' -f2)
          echo "EXEC $procs_parents -- $procs_sons -- $adr_perc -- $ibarrier_use -- $phy_dist -- $cst -- $css -- RUN $i"

          for index in 1 2 3 4 5 6 7 8 9 10
          do

            mpirun -f hostfile.o$SLURM_JOB_ID $dir$codeDir/./bench.out $configFile $i $nodelist $nodes
            rm hostfile.o$SLURM_JOB_ID
          done

        done
      done
    done  
  done
done
echo "END TEST"
sed -i 's/application called MPI_Abort(MPI_COMM_WORLD, -100) - process/shrink cleaning/g' slurm-$SLURM_JOB_ID.out
