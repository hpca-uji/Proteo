#!/bin/bash

dir="/home/martini/malleability_benchmark/"
codeDir="Codes/"
execDir="Exec/"
ResultsDir="Results/"

ResultsDirName=$1
maxIndex=$2

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

  while IFS="" read -r line || [ -n "$line" ]
  do
    run=$(echo $line | cut -d '/' -f1 | cut -d 'n' -f2)
    echo "Run $run"
    index=$(($run + $maxIndex))
    sbatch -N 2 $dir$execDir./singleRun.sh config$run.ini $index
  done < errores.txt
fi
