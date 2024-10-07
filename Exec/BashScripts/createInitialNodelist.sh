#!/bin/bash

# Gets the first nodelist that will be used during the emulation
# Parameter 1(Optional) - Amount of executions per file. Must be a positive number
#====== Do not modify these values =======

numP=$1
cores=$2
nodelist=$3

initial_node_qty=$(($numP / $cores))
if [ $initial_node_qty -eq 0 ]
then
  initial_node_qty=1
fi

common_node_name="n" #FIXME What if it uses another type of node?
if [[ $nodelist == *"["* ]]; then
  common_node_name=$(echo $nodelist | cut -d '[' -f1)
fi

node_array=($(echo $nodelist | sed -e 's/[\[${common_node_name}]//g' -e 's/\]/ /g' -e 's/,/ /g'))
actual_node_qty=0
for ((i=0; $actual_node_qty<$initial_node_qty; i++))
do
  element=($(echo ${node_array[$i]} | sed -e 's/-/ /g'))

  nodes_qty=1
  if [ "${#element[@]}" -gt 1 ];
  then
    nodes_qty=$((10#${element[1]}-10#${element[0]}+1))
  fi

  expected_node_qty=$(($actual_node_qty + $nodes_qty))
  if [ "$expected_node_qty" -le "$initial_node_qty" ];
  then
    added_qty=$nodes_qty
    actual_node_qty=$expected_node_qty
  else
    added_qty=$(($initial_node_qty - $actual_node_qty))
    actual_node_qty=$initial_node_qty
  fi

  for ((j=0; j<$added_qty; j++))
  do
    index=$((10#${element[0]} + $j))
    index=0$index # FIXME What if there are more than 9 nodes?
    #FIXME What if less than $cores have to be spawned?
    for ((core=0; core<$cores; core++)) # FIXME What if the user asks for a spread distribution
    do
      initial_nodelist="${initial_nodelist:+$initial_nodelist,}"$common_node_name$index
    done
  done
done

#Print result
echo $initial_nodelist
