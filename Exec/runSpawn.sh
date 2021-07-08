#!/bin/bash

dir="/home/martini/malleability_benchmark/"
codeDir="Codes/"
execDir="Exec/"
ResultsDir="Results/"

#TODO Añadir diferenciar phy_dist de padres e hijos al ejecutar
echo "START TEST"

groups=$1 #TODO Modificar para que admita más de dos grupos de procesos
matrix_tam=100000
comm_tam=0
N_qty=0 # Datos a redistribuir
time=0.5
proc_init=2 #El tiempo por iteracion es para esta cantidad de procesos
iters=100
node_qty=$2

max_procs=$(($node_qty * 20))
procs_array=(2 10)

#Obtener cantidades de procesos posibles a ejecutar
i=0
while [[ $value -lt $max_procs ]]
do
  i=$(($i + 1))
  value=$((20 * $i))
  procs_array=(${procs_array[@]} $value)
done

#Crear carpeta de resultados
cd $dir$ResultsDir
name_res="S-"$node_qty"N-"$(date '+%m-%d')
if [ -d $name_res ] # Si ya existe el directorio, modificar levemente el nombre y crear otro
then
  name_res=$name_res"-"$(date '+%H:%M')
fi
echo "Localizacion de los resultados: $dir$ResultsDir$name_res"
mkdir $name_res

# Ejecutar pruebas
i=0
j=0
for procs_parents in "${procs_array[@]}"
do
  for procs_sons in "${procs_array[@]}"
  do
    for phy_dist in cpu node
    do
      i=$(($i + 1))

      # Crear directorio para esta ejecucion
      cd $dir$ResultsDir$name_res
      mkdir Run$i
      cd Run$i

      # Crear archivo de configuracion
      echo "Config $procs_parents -- $procs_sons -- $adr_perc -- $ibarrier_use -- $phy_dist -- RUN $i"
      array0=($iters $procs_parents $phy_dist)
      array=("${array0[@]}")
      array0=($iters $procs_sons $phy_dist)
      array+=("${array0[@]}")
      python3 $dir$execDir/./create_ini.py config$i.ini 1 $matrix_tam $comm_tam $N_qty $adr_perc $ibarrier_use $time $proc_init "${array[@]}"
    done

    aux=$(($j * 10))
    # LANZAR SCRIPT
    echo $aux
    sbatch -N $node_qty $dir$execDir./arraySpawnRun.sh $dir$ResultsDir$name_res $aux $procs_parents $procs_sons
    j=$(($j + 1))
  done
done
