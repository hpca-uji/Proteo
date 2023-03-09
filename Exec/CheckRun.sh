#!/bin/bash

dir="/home/usuario/Documentos/malleability_benchmark"
cores=20

# Checks if all the runs in the current working directory performed under a 
# Slurm manager have been performed correctly and if some runs can be corrected 
# they are launched again
# Parameter 1 - Maximum index of the runs
# Parameter 2 - Amount of repetitions per index/run
# Parameter 3 - Total stages in all runs. #FIXME The amount of stages must be equal across all the runs, must be modified in the future.
# Parameter 4 - Total groups of processes in all runs #FIXME The amount of groups must be equal across all the runs, must be modified in the future.
# Parameter 5 - Maximum valid iteration time across all runs. If an iteration time
#               is higher, that particular repetition inside the run is cleaned and
#               launched again.
#====== Do not modify the following values =======

codeDir="Codes/"
execDir="Exec/"
ResultsDir="Results/"

maxIndex=$1
totalEjGrupo=$2 #Total de ejecuciones por grupo
total_stages=$3
total_groups=$4
maxTime=$5 #Maximo tiempo que se considera válido

exec_lines_basic=6
iter_lines_basic=3
exec_total_lines=$(($exec_lines_basic+$total_stages+$total_groups))
iter_total_lines=$(($iter_lines_basic+$total_stages*2+1))
exec_remove=$(($exec_lines_basic+$total_stages+$total_groups-1))
iter_remove=$(($iter_lines_basic+$total_stages-1))

echo $#
if [ "$#" -lt "5" ]
then
  echo "Not enough arguments"
  echo "Usage -> bash CheckRun maxIndes total_repetitions total_groups total_stages max_iteration_time"
  exit -1
fi

#Check if there are fatal errors during executions
grep -i -e fatal -e error -e abort -e == slurm* > errores2.txt
qty=$(wc -l errores2.txt | cut -d ' ' -f1)
if [ "$qty" -gt "0" ]
then
  echo "Found Fatal errors during execution. Aborting"
  echo "Read file errors2 to see the errors and in which files"
  exit -2
fi

#Check if the number of output files is correct.
#If the number is not correct is a fatal error and the user
# is informed in which runs the amount does not match, and
# then the scripts exit.
#The user must figure out what to do with those runs.
qtyG=$(ls R*_Global.out | wc -l)
qtyG=$(($qtyG * 2))
qtyL=$(ls R*_G*N*.out | wc -l)
if [ "$qtyG" == "$qtyL" ]
then
  echo "Number of G($qtyG) and L($qtyL) files match"
else
  echo "Lacking Local($qtyL) or global($qtyG) files. Aborting"
  echo "Lacking Local($qtyL) or global($qtyG) files. Aborting" > errores2.txt
  for ((i=0; i<$maxIndex; i++))
  do
    qtyEx=$(grep T_total R"$i"_Global.out | wc -l)
    qtyIt=$(grep T_iter R"$i"_G*N*.out | wc -l)
    qtyEx=$(($qtyEx * 2))
    if [ "$qtyEx" -ne "$qtyIt" ] 
    then
      diff=$(($totalEjGrupo-$qtyEx))
      echo "Files do not match at Run $i -- diff=$diff"
      echo "Files do not match at Run $i -- diff=$diff" >> errores2.txt
    fi
  done
  exit -1
fi
rm errores2.txt

# Check if there is any negative execution time
# Only invalid IDs are stored
rm -f errores.txt
touch errores.txt
exec_ids=($(grep -n "T_total" R*_Global.out | grep - | cut -d '_' -f1 | cut -d 'R' -f2))
exec_line=($(grep -n "T_total" R*_Global.out | grep - | cut -d ':' -f2))
for ((i=${#exec_ids[@]}-1; i>=0; i--))
do
  first_line=$((${exec_line[$i]}-$exec_remove))
  last_line=$(($first_line+$exec_total_lines-1))
  echo "${exec_ids[$i]}:$first_line:$last_line" >> errores.txt
done

# Check if there is any iter time higher than expected
# Only invalid IDs are stored
iter_times=($(grep "T_iter" R*_G*N*.out | cut -d ' ' -f2))
iter_ids=($(grep "T_iter" R*_G*N*.out | cut -d '_' -f1 | cut -d 'R' -f2))
iter_line=($(grep -n "T_iter" R*_G*N*.out | cut -d ':' -f2))
for ((i=${#iter_times[@]}-1; i>=0; i--))
do
  is_invalid=$(echo ${iter_times[$i]}'>'$maxTime | bc -l)
  if [ $is_invalid -eq 1 ]
  then
    first_line=$((${iter_line[$i]}-$iter_remove))
    # Translate line number to Global file
    first_line=$(($first_line/$iter_total_lines))
    first_line=$(($first_line*$exec_total_lines+1))
    last_line=$(($first_line+$exec_total_lines-1))
    echo "${iter_ids[$i]}:$first_line:$last_line" >> errores.txt
  fi
done

#Clean data from collected erroneous executions
qty=$(wc -l errores.txt | cut -d ' ' -f1)
if [ "$qty" -gt 0 ];
then
  echo "Se han encontrado errores de ejecución leves. Volviendo a ejecutar"

  while IFS="" read -r lineRun || [ -n "$lineRun" ]
  do
    #Obtain data of erroneous execution
    run=$(echo $lineRun | cut -d ':' -f1)
    echo "Run $run had an erroneous execution, cleaning bad data."

    #1 - Delete erroneous lines in Global file
    first_line=$(echo $lineRun | cut -d ':' -f2)
    last_line=$(echo $lineRun | cut -d ':' -f3)
    sed -i ''$first_line','$last_line'd' R${run}_Global.out

    #2 - Translate line numbers to Local files type
    first_line=$(($first_line/$exec_total_lines))
    first_line=$(($first_line*$iter_total_lines+1))
    last_line=$(($first_line+$iter_total_lines-1))
    #3 - Delete erroneous lines in Local files
    for ((j=0; j<total_groups; j++)); 
    do
      sed -i ''$first_line','$last_line'd' R${run}_G${j}*
    done

  done < errores.txt
fi

#Check if all repetitions for each Run have been executed
#If any run lacks repetitions, the job is automatically launched again
#If a run has even executed a repetition, is not launched as it could be in the waiting queue
qty_missing=0
for ((run=0; run<$maxIndex; run++))
do
  if [ -f "R${run}_Global.out" ]
  then
    qtyEx=$(grep T_total R"$run"_Global.out | wc -l)
    if [ "$qtyEx" -ne "$totalEjGrupo" ];
    then
      diff=$(($totalEjGrupo-$qtyEx))
      qty_missing=$(($qty_missing+$diff))
      config_file="config$run.ini"

      #1 - Obtain maximum number of processes for the run
      max_numP=-1
      for ((j=0; j<total_groups; j++)); 
      do
        resize_info=$(grep "\[resize$j\]" -n $config_file | cut -d ":" -f1)
        first_line=$(echo $resize_info | cut -d " " -f1)
        last_line=$(echo $resize_info | cut -d " " -f2)
        range_lines=$(( last_line - first_line ))
        numP=$(head -$last_line $config_file | tail -$range_lines | cut -d ';' -f1 | grep Procs | cut -d '=' -f2)
	if [ "$numP" -gt "$max_numP" ];
	then
	  max_numP=$numP
	fi
      done

      #3 - Obtain needed nodes for the number of processes
      node_qty=$(($max_numP / $cores))
      if [ "$node_qty" -eq "0" ];
      then
        node_qty=1
      fi

      #3 - Launch execution
      echo "Run$run lacks $diff repetitions"
      use_extrae=0
      sbatch -N $node_qty $dir$execDir./generalRun.sh $dir $config_file $use_extrae $run $diff
    fi
  else
    echo "File R${run}_Global.out does not exist -- Could it be it must still be executed?"
  fi
done


if [ "$qty_missing" -eq "0" ];
then
  echo "SUCCESS"
else
  echo "REPEATING - A total of $qty_missing executions are being repeated"
fi
