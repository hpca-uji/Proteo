#!/bin/bash

dir="/home/martini/malleability_benchmark/Codes/auxiliar_codes"

aux=$(grep "\[resize0\]" -n $1 | cut -d ":" -f1)
read -r ini fin <<<$(echo $aux)
diff=$(( fin - ini ))
numP=$(head -$fin $1 | tail -$diff | cut -d ';' -f1 | grep procs | cut -d '=' -f2)
dist=$(head -$fin $1 | tail -$diff | cut -d ';' -f1 | grep physical_dist | cut -d '=' -f2)

if [ $dist == "node" ]; then
    dist=1
elif [ $dist == "cpu" ]; then
    dist=2
fi

$dir/Recordnodelist.o $numP $dist
echo $numP
