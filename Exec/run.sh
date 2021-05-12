#!/bin/bash

dir="/home/martini/malleability_benchmark/"
codeDir="Codes/"
execDir="Exec/"
ResultsDir="Results/"

#TODO Añadir diferenciar phy_dist de padres e hijos al ejecutar
echo "START TEST"

groups=$1 #TODO Modificar para que admita más de dos grupos de procesos
matrix_tam=$2
N_qty=$3
time=$4
proc_init=$5 #El tiempo por iteracion es para esta cantidad de procesos
iters=$6
node_qty=$7

max_procs=$(($node_qty * 20))
procs_array=()
percs_array=(0 25 50 75 100)
i=1
value=$((2 ** $i))
procs_array=(${procs_array[@]} $value)

#Obtener cantidades de procesos posibles a ejecutar
while [[ $value -le $max_procs ]]
do
  i=$(($i + 1))
  value=$((2 ** $i))
  procs_array=(${procs_array[@]} $value)
done
unset procs_array[-1]

#Crear carpeta de resultados
cd $dir$ResultsDir
name_res=$node_qty"N-"$(date '+%m-%d')
echo "Localizacion de los resultados: $dir$ResultsDir$name_res"
mkdir $name_res

# Ejecutar pruebas
i=0
for procs_parents in "${procs_array[@]}"
do
  for procs_sons in "${procs_array[@]}"
  do
    if [ $procs_sons -ne $procs_parents ]
    then

      for adr_perc in "${percs_array[@]}"
      do

        for phy_dist in cpu node
        do

          for ibarrier_use in 0 #TODO Poner a 0 1
          do
            i=$(($i + 1))

	    # Crear directorio para esta ejecucion
            cd $dir$ResultsDir$name_res
	    name_run="Run$i"
	    mkdir Run$i
	    cd Run$i

            # Crear archivo de configuracion
	    echo "$procs_parents -- $procs_sons -- $adr_perc -- $ibarrier_use -- $phy_dist -- RUN $i"
            array0=($iters $procs_parents $phy_dist)
            array=("${array0[@]}")
            array0=($iters $procs_sons $phy_dist)
            array+=("${array0[@]}")
            python3 $dir$execDir/./create_ini.py config$i.ini 1 $matrix_tam $N_qty $adr_perc $ibarrier_use $time $proc_init "${array[@]}"

	    # LANZAR SCRIPT
            sbatch -N $node_qty $dir$execDir./runResults.sh config$i.ini $i
          done
        
        done
        
      done

    fi
  done
done

echo "END TEST"
