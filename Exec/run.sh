#!/bin/bash

dir="/home/martini/malleability_benchmark/"
codeDir="Codes/"
execDir="Exec/"
ResultsDir="Results/"

#TODO Añadir diferenciar phy_dist de padres e hijos al ejecutar
#TODO Añadir que se considere la cantidad de nucleos de un nodo y no este fijada

if [[ $# -lt 9 ]]
then
  echo "Faltan argumentos"
  echo "bash run.sh grupos tam_computo tam_comm tam_resize tiempo proc_init iters first_iter node_qty"
  exit -1
fi

echo "START TEST"

groups=$1 #TODO Modificar para que admita más de dos grupos de procesos
matrix_tam=$2
comm_tam=$3
N_qty=$4 # Datos a redistribuir
time=$5
proc_init=$6 #El tiempo por iteracion es para esta cantidad de procesos
iters=$7
first_iter=$8
node_qty=$9

# Si el valor es 0 el primer grupo de procesos realiza $iters iteraciones antes de redimensionar
if [[ $first_iter -eq 0 ]] 
then
  iters_first_group=$iters
# Si el valor es diferente a 0, el primer grupo de procesos realiza $first_iter iteraciones antes de redimensionar
else 
  iters_first_group=$first_iter
fi
max_procs=$(($node_qty * 20))
procs_array=(2 10)
#percs_array=(0 25 50 75 100)
percs_array=(0)
at_array=(3)

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
name_res=$node_qty"N-"$(date '+%m-%d')
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
    for adr_perc in "${percs_array[@]}"
    do

      for phy_dist in cpu node
      do

        for ibarrier_use in "${at_array[@]}"
        do
          i=$(($i + 1))

	  # Crear directorio para esta ejecucion
          cd $dir$ResultsDir$name_res
	  mkdir Run$i
	  cd Run$i

          # Crear archivo de configuracion
	  echo "Config $procs_parents -- $procs_sons -- $adr_perc -- $ibarrier_use -- $phy_dist -- RUN $i"
          array0=($iters_first_group $procs_parents $phy_dist)
          array=("${array0[@]}")
          array0=($iters $procs_sons $phy_dist)
          array+=("${array0[@]}")
          python3 $dir$execDir/./create_ini.py config$i.ini 1 $matrix_tam $comm_tam $N_qty $adr_perc $ibarrier_use $time $proc_init "${array[@]}"

        done
      done
    done
    start_i=$(($j * ${#percs_array[@]} * ${#at_array[@]} * 2)) #TODO modficar utlimo valor conforme cambie phy_dist
    # LANZAR SCRIPT
    echo $aux
    sbatch -N $node_qty $dir$execDir./arrayRun.sh $dir$ResultsDir$name_res $start_i $procs_parents $procs_sons
    j=$(($j + 1))
  done
done

echo "Localizacion de los resultados: $dir$ResultsDir$name_res"
echo "END TEST"
