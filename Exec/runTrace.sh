dir="/home/martini/malleability_benchmark"
#procs=(2 10 20 40 80 120 160)
procs=(2)
cores=20


#====== Do not modify these values =======

codeDir="/Codes/build"
execDir="/Exec"
ResultsDir="/Results"
use_extrae=1

#Extrae config
qty=1
outFileIndex=$2

if [ $# -ge 1 ]
then
  qty=$1
fi

for proc in "${procs[@]}"
do
  echo "------------------------------------------run np=$proc"

  node_qty=$(($proc / $cores))
  if [ $node_qty -eq 0 ]
  then
    node_qty=1
  fi

  config_file="test$proc"".ini"
  upper_dir="Proc$proc"
  mkdir $upper_dir
  cd $upper_dir
  for ((i=0; i<qty; i++))
  do
    # Move data to new directory
    lower_dir="Run$i"
    mkdir $lower_dir
    cd $lower_dir
    cp $dir$execDir/extrae.xml .
    cp $dir$execDir/trace.sh .
    cp ../../$config_file .

    #pwd
    sbatch -p P1 -N $node_qty $dir$execDir/generalRun.sh $dir $config_file $use_extrae $outFileIndex
    cd ..
  done
  cd ..

done
echo "End"
