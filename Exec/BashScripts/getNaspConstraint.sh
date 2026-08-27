#!/bin/bash

# Builds a constraint string to be used in Nasp for an heterogenous
# list, ensuring the amount are fixed. Therefore, this script is used
# to ensure the allocated resources are fixed for experiments.
# Parameter 1 - Number of nodes per node type. String of integers separated by commas
# Parameter 2 - Nodes names. String of names separated by commas
#====== Do not modify these values =======

nodes_cores_aux=$1
nodes_names_aux=$2

if [ "$#" -lt "2" ]
then
  echo "Not enough arguments"
  echo "Usage -> bash getNaspConstraint.sh NodeQtys NodeNames"
  exit -1
fi

IFS=',' read -ra node_qties <<< "$nodes_cores_aux"
IFS=',' read -ra nodes_names <<< "$nodes_names_aux"

constraint_str=""
for ((i=0; i<${#node_qties[@]}; i++)); do
  type_count=${node_qties[i]}

  label=$(sinfo -n "${nodes_names[i]}" -o "%f" | tail -1)

  if [[ "$label" != "(null)" ]]; then
    constraint_part="${label}*${type_count}"
    if [[ -z "$constraint_str" ]]; then
      constraint_str="$constraint_part"
    else
      constraint_str="$constraint_str&$constraint_part"
    fi
  fi
done

if [[ -z "$constraint_str" ]]; then
    echo ""
else
    echo "[$constraint_str]"
fi