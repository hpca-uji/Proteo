#!/bin/bash

dir="/home/martini/malleability_benchmark/"
codeDir="Codes/"
execDir="Exec/"
ResultsDir="Results/"

ResultsDirName=$1
maxIndex=$2
cantidadGrupos=$3 #Contando a los padres
totalEjGrupo=$4 #Total de ejecuciones por grupo
maxTime=$5 #Maximo tiempo que se considera válido

if [ $# -lt 3 ]
then
  echo "Faltan argumentos"
  echo "Uso -> bash CheckRun NombreDirectorio IndiceMaximo Grupos"
  exit -1
fi

cd $dir$ResultsDir
if [ ! -d $ResultsDirName ]
then
  echo "La carpeta de resultados $ResultsDirName no existe. Abortando"
  exit -1
fi
cd $ResultsDirName

#Comprobar si hay errores
#Si los hay, salir
grep -i -e fatal -e error -e abort -e == */slurm* > errores2.txt
qty=$(wc -l errores2.txt | cut -d ' ' -f1)

if [ $qty -gt 0 ]
then
  echo "Se han encontrado errores de ejecución graves. Abortando"
  echo "Revisar archivo errores2.txt en el directorio $ResultsDirName"
  exit -2
fi
rm errores2.txt

qtyG=$(ls R*/R*_Global.out | wc -l)
qtyG=$(($qtyG * 2))
qtyL=$(ls R*/R*_G?N*.out | wc -l)
if [ $qtyG == $qtyL ]
then
  echo "El numero de ficheros G($qtyG) y L($qtyL) coincide"
else
  #Si faltan archivos, se indican cuales faltan
  echo "Faltan ejecuciones Locales o globales"
  for ((i=1; i<$maxIndex; i++))
  do
    qtyEx=$(grep Tex -r Run$i | wc -l)
    qtyIt=$(grep Top -r Run$i | wc -l)
    qtyEx=$(($qtyEx * 2))
    if [ $qtyEx -ne $qtyIt ] 
    then
      diff=$(($totalEjGrupo-$qtyEx))
      echo "Faltan archivos en Run$i"
    fi
  done
  exit -1
fi

#grep -rn "2.\." R* TODO Testear que el tiempo teorico maximo es valido?

#Comprobar si hay runs con tiempo negativos
#Si los hay, reejecutar e informar de cuales son
grep - */R* | grep Tex > errores.txt
qty=$(wc -l errores.txt | cut -d ' ' -f1)
if [ $qty -gt 0 ]
then
  echo "Se han encontrado errores de ejecución leves. Volviendo a ejecutar"

  while IFS="" read -r lineRun || [ -n "$lineRun" ]
  do
    #Obtener datos de una ejecución erronea
    run=$(echo $lineRun | cut -d 'R' -f3 | cut -d '_' -f1)
    if [ $run -gt $maxIndex ]
    then #Indice de ejecuciones posteriores echas a mano -- FIXME Eliminar?
      realRun=$(($run - $maxIndex))
      index=$run
    else # Indice de las primeras ejecuciones
      realRun=$run
      index=$(($run + $maxIndex))
    fi

    echo "Run $run"
    cd Run$realRun

    #Arreglar ejecuccion

    #1 - Borrar lineas erroneas
    qty=$(grep -n - R* | grep Tex | wc -l)
    for ((i=0; i<qty; i++))
    do 
      fin=$(grep -n - R* | grep Tex | cut -d ':' -f2 | head -n1)
      init=$(($fin - 6))
      sed -i ''$init','$fin'd' R${realRun}_Global.out

      aux=$(($fin / 7)) #Utilizado para saber de entre las ejecuciones del fichero, cual es la erronea
      fin=$(($aux * 5))
      init=$(($fin - 4))
      for ((j=0; j<cantidadGrupos; j++)); do
        sed -i ''$init','$fin'd' R${realRun}_G${j}*
      done
    done

    #2 - Reelanzar ejecucion
    proc_list=$(grep Procs R${realRun}_Global.out | cut -d '=' -f3 | cut -d ',' -f1)
    proc_parents=$(echo $proc_list | cut -d ' ' -f1)
    proc_children=$(echo $proc_list | cut -d ' ' -f2)
    nodes=8 # Maximo actual
    if [ $procs_parents -gt $procs_children ]
    then
      nodes=$(($procs_parents / 20))
    else
      nodes=$(($procs_children / 20))
    fi

    sbatch -N $nodes $dir$execDir./singleRun.sh config$realRun.ini $index
    cd $dir$ResultsDir$ResultsDirName

  done < errores.txt
  exit 0
fi

#Comprobar que todas las ejecuciones tienen todas las ejecucciones que tocan
#Solo es necesario comprobar el global.
qty_missing=0
for ((i=1; i<$maxIndex; i++))
do
  qtyEx=$(grep Tex -r Run$i | wc -l)
  if [ $qtyEx -ne $totalEjGrupo ]
  then
    diff=$(($totalEjGrupo-$qtyEx))
    qty_missing=$(($qty_missing+1))
    echo "Faltan en $i, $diff ejecuciones"
  fi
done

if [ $qty_missing -eq 0 ]
then
  echo "Todos los archivos tienen $totalEjGrupo ejecuciones"
fi
