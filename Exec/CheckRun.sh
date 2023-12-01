#!/bin/bash

partition="P1"

# Checks if all the runs in the current working directory performed under a 
# Slurm manager have been performed correctly and if some runs can be corrected 
# they are launched again
# Parameter 1 - Common name of the configuration files
# Parameter 2 - Maximum index of the runs
# Parameter 3 - Amount of repetitions per index/run
# Parameter 4 - Total stages in all runs. #FIXME The amount of stages must be equal across all the runs, must be modified in the future.
# Parameter 5 - Total groups of processes in all runs #FIXME The amount of groups must be equal across all the runs, must be modified in the future.
# Parameter 6 - Maximum valid iteration time across all runs. If an iteration time
#               is higher, that particular repetition inside the run is cleaned and
#               launched again.
# Parameter 7(Optional) - Maximum amount of time in seconds needed by a single execution. Default value is 0, which indicates infinite time. 
#               Must be a positive integer.
#====== Do not modify the following values =======

scriptDir="$(dirname "$0")"
source $scriptDir/../Codes/build/config.txt
codeDir="/Codes/"
execDir="/Exec/"
ResultsDir="/Results/"
cores=$(bash $dir$execDir/BashScripts/getCores.sh $partition)

if [ "$#" -lt "6" ]
then
  echo "Not enough arguments"
  echo "Usage -> bash CheckRun Common_Name maxIndex total_repetitions total_groups total_stages max_iteration_time [limit_time]"
  exit -1
fi

common_name=$1
maxIndex=$2
totalEjGrupo=$3 #Total de ejecuciones por grupo
total_stages=$4
total_groups=$5
maxTime=$6 #Maximo tiempo que se considera válido
limit_time_exec=0
if [ $# -ge 7 ] #Max time per execution in seconds
then
  limit_time_exec=$7
fi

limit_time=0
exec_lines_basic=7
iter_lines_basic=3
exec_total_lines=$(($exec_lines_basic+$total_stages+$total_groups))
iter_total_lines=$(($iter_lines_basic+$total_stages*2+1))
exec_remove=$(($exec_lines_basic+$total_stages+$total_groups-1))
iter_remove=$(($iter_lines_basic+$total_stages))


#Check if there are fatal errors during executions
grep -i -e fatal -e error -e abort -e == slurm* > errores2.txt
qty=$(wc -l errores2.txt | cut -d ' ' -f1)
if [ "$qty" -gt "0" ]
then
  echo "Found Fatal errors during execution. Aborting"
  echo "Read file errors2 to see the errors and in which files"
  echo "FAILURE"
  exit -2
fi

#Check if the number of output files is correct.
#If the number is not correct is a fatal error and the user
# is informed in which runs the amount does not match, and
# then the scripts exits.
#The user must figure out what to do with those runs.
qtyG=$(ls R*_Global.out | wc -l)
qtyG=$(($qtyG * $total_groups))
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
  echo "FAILURE"
  exit -1
fi
rm errores2.txt

# Check if there is any negative execution time
# Only invalid IDs are stored
rm -f tmp.txt
touch tmp.txt
exec_ids=($(grep -n "T_total" R*_Global.out | grep - | cut -d '_' -f1 | cut -d 'R' -f2))
exec_line=($(grep -n "T_total" R*_Global.out | grep - | cut -d ':' -f2))
for ((i=${#exec_ids[@]}-1; i>=0; i--))
do
  first_line=$((${exec_line[$i]}-$exec_remove))
  last_line=$(($first_line+$exec_total_lines-1))
  echo "${exec_ids[$i]}:$first_line:$last_line" >> tmp.txt
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
    echo "${iter_ids[$i]}:$first_line:$last_line" >> tmp.txt
  fi
done

#Clean data from collected erroneous executions
qty=$(wc -l tmp.txt | cut -d ' ' -f1)
if [ "$qty" -gt 0 ];
then
  echo "Found minor execution errors. Executing again. Review file errores.txt."
  echo "CHECKRUN -- Found errors" >> errores.txt

  while IFS="" read -r lineRun || [ -n "$lineRun" ]
  do
    #Obtain data of erroneous execution
    run=$(echo $lineRun | cut -d ':' -f1)
    echo "Run $run had an erroneous execution, cleaning bad data."
    echo "Run$run----------------------------------------------" >> errores.txt

    #1 - Delete erroneous lines in Global file
    first_line=$(echo $lineRun | cut -d ':' -f2)
    last_line=$(echo $lineRun | cut -d ':' -f3)

    sed -n ''$first_line','$last_line'p' R${run}_Global.out >> errores.txt
    sed -i ''$first_line','$last_line'd' R${run}_Global.out

    #2 - Translate line numbers to Local files type
    first_line=$(($first_line/$exec_total_lines))
    first_line=$(($first_line*$iter_total_lines+1))
    last_line=$(($first_line+$iter_total_lines-1))
    #3 - Delete erroneous lines in Local files
    for ((j=0; j<total_groups; j++)); 
    do
      sed -n ''$first_line','$last_line'p' R${run}_G${j}* >> errores.txt
      sed -i ''$first_line','$last_line'd' R${run}_G${j}*
    done
    echo "--------------------------------------------------" >> errores.txt

  done < tmp.txt
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
      #1 - Obtain config file name and repetitions to perform
      diff=$(($totalEjGrupo-$qtyEx))
      qty_missing=$(($qty_missing+$diff))
      config_file="$common_name$run.ini"
      if [ $limit_time_exec -ne 0 ] #Max time per execution in seconds
      then
        limit_time=$(($limit_time_exec*$diff/60+1))
      fi

      #2 - Obtain number of nodes needed
      node_qty=$(bash $dir$execDir/BashScripts/getMaxNodesNeeded.sh $config_file $dir $cores)

      #3 - Launch execution
      echo "Run$run lacks $diff repetitions"
      use_extrae=0
      sbatch -p $partition -N $node_qty -t $limit_time $dir$execDir./generalRun.sh $dir $cores $config_file $use_extrae $run $diff
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
