#!/bin/bash

# !!!!This script should only be called by MaM library!!!!
# Expands with the new given resources the .
# Parameter 1 (--numP) - Number of processes to spawn
#====== Do not modify these values =======

mandatory_arg="--numP="
other_args=()
numP=""

source $PROTEO_HOME/Codes/build/config.txt
echo "Expanded JOB within MaM"

for arg in "$@"; do
  if [[ "$arg" == "$mandatory_arg="* ]]; then
    numP="${arg#*=}"
  else
    other_args+=("$arg")
  fi
done
if [ -z "$numP" ]; then
  echo "Warning: The number of ranks to spawn has not been suplied. Trying to obtain from SLURM ENV" >&2

  if [ -z "$SLURM_JOB_CPUS_PER_NODE" ]; then
    echo "Critical Failure: SLURM ENV SLURM_JOB_CPUS_PER_NODE not present. It is not possible to obtain the number of ranks to start" >&2
    echo "Usage: $0 $mandatory_arg=integer [other args...]" >&2
    exit 1
  fi
  numP=$(echo "$SLURM_JOB_CPUS_PER_NODE" \
    | awk -F, '{sum=0; for(i=1;i<=NF;i++) { if($i ~ /x/) { split($i,a,"[(x)]"); sum+=a[1]*a[3] } else { sum+=$i } } print sum}')

  if [[ $numP -le 0 ]]; then
    echo "Critical Failure: SLURM ENV SLURM_JOB_CPUS_PER_NODE=$SLURM_JOB_CPUS_PER_NODE is present but empty?. It is not possible to obtain the number of ranks to start" >&2
    echo "Usage: $0 $mandatory_arg=integer [other args...]" >&2
    exit 1
  fi
fi

initial_nodelist=$(bash $PROTEO_HOME$execDir/BashScripts/createInitialNodelist.sh $numP)

mpirun -hosts $initial_nodelist -np $numP $PROTEO_BIN
echo "END RUN"