#!/bin/bash

# Obtains for a given configuration file how many nodes will be needed
# Parameter 1 - Configuration file name for the emulation.
# Parameter 2 - Partition to use
# FIXME: Not tested for shared systems
# FIXME: Does not correctly balance out the amount of nodes in all casses
# NOTE: Actual script tries to always balance out the types of node used
#====== Do not modify these values =======

execDir="/Exec"
ignore='c'

if [ "$#" -lt "2" ]
then
  echo "Not enough arguments"
  echo "Usage -> bash getMaxNodesNeeded.sh Configuration.ini Partition"
  exit -1
fi

config_file=$1
partition=$2

max_numP=-1
total_resizes=$(grep Total_Resizes $config_file | cut -d '=' -f2)
total_groups=$(($total_resizes + 1))
for ((j=0; j<total_groups; j++));
do
  numP=$(bash $PROTEO_HOME$execDir/BashScripts/getNumPNeeded.sh $config_file $j)
  if [ "$numP" -gt "$max_numP" ];
  then
    max_numP=$numP
  fi
done
#node_qty=$(( ($max_numP + $cores - 1) / $cores ))

# Get partition data
cores=()
nodes_names=()
node_qties=()
node_qty=0

partition_string=$(sinfo --partition $partition -o "%n %c" | sed '/HOSTNAMES/d' | grep -v -E $ignore | sort -k2 | uniq -f 1| paste -d';' -s)
length=$(awk -F";" '{print NF}' <<< "${partition_string}")
for ((i=0; i<"$length"; i++)); do 
  search=$((i+1))
  nodes_names[i]=$(echo "$partition_string" | cut -d ';' -f$search | cut -d ' ' -f1); 
  cores[i]=$(echo "$partition_string" | cut -d ';' -f$search  | cut -d ' ' -f2); 
done

# Compute required amount of nodes
sum_cores=0
for core_i in "${cores[@]}"; do
  sum_cores=$((sum_cores + core_i))
done

node_qty_aux=$((max_numP / sum_cores))
remainder=$((max_numP % sum_cores))

if [ $node_qty_aux -ne 0 ]
then
  for ((i=0; i<"$length"; i++)); do 
    node_qty=$((node_qty_aux + node_qty))
    node_qties[i]=$node_qty_aux
  done
fi

# Add aditional nodes if is not divisable
i=0
while (( remainder > 0 )); do
  node_qties[i]=$((1 + node_qties[i]))
  node_qty=$((1 + node_qty))
  remainder=$((remainder - cores[i]))
done

if [ $node_qty -eq 0 ]
then
  node_qty=1
fi

# Get constraint for experimental setup
node_qties_coma=$(echo "${node_qties[@]}" | sed "s/ /,/g")
nodes_names_coma=$(echo "${nodes_names[@]}" | sed "s/ /,/g")
constraint=$(bash $PROTEO_HOME$execDir/BashScripts/getNaspConstraint.sh $node_qties_coma $nodes_names_coma)

echo "$node_qty,$constraint"
