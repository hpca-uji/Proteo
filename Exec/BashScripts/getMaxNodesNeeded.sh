#!/bin/bash

# FIXME: Does not work for shared/heterogenous systems
# Obtains for a given configuration file how many nodes will be needed
# Parameter 1 - Configuration file name for the emulation.
# Parameter 2 - Number of cores in the machines. The machines must be homogenous. Must be a positive number.
#====== Do not modify these values =======

execDir="/Exec"

if [ "$#" -lt "2" ]
then
  echo "Not enough arguments"
  echo "Usage -> bash getMaxNodesNeeded.sh Configuration.ini NumCores"
  exit -1
fi

config_file=$1
cores=$2

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
node_qty=$(($max_numP / $cores))
if [ $node_qty -eq 0 ]
then
  node_qty=1
fi

echo $node_qty
