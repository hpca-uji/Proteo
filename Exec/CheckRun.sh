#!/bin/bash

dir="/home/martini/malleability_benchmark/"
codeDir="Codes/"
execDir="Exec/"
ResultsDir="Results/"

ResultsDirName=$1
maxIndex=$2
cantidadGrupos=$3 #Contando a los padres

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
  exit -2
fi
rm errores2.txt

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
    run=$(echo $lineRun | cut -d '/R' -f2 | cut -d '_' -f1)
    if [ $run -gt $maxIndex ]
    then #Indice de ejecuciones posteriores
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
      ini=$(($fin - 4))
      for ((j=0; j<cantidadGrupos; j++)); do
        sed -i ''$init','$fin'd' R${realRun}_G${j}*
      done
    done

    #2 - Reelanzar ejecucion
    sbatch -N 2 $dir$execDir./singleRun.sh config$realRun.ini $index
    cd $dir$ResultsDir$ResultsDirName

  done < errores.txt
fi
