dir="/home/martini/malleability_benchmark"
partition="P1"
exclude="c00,c01,c02"
procs=(2 10 20 40 80 120 160)
cores=20

#Runs in a given current directory all .ini files that contains 
# in the name an element of the array "procs".
# Parameter 1(Optional) - Amount of executions per file. Must be a positive number
# Parameter 2(Optional) - Index to use for the resulting files of the execution. Must be a number.
#====== Do not modify these values =======

codeDir="/Codes/build"
execDir="/Exec"
ResultsDir="/Results"
use_extrae=0

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
  for ((i=0; i<qty; i++))
  do
    #Execute test
    sbatch -p $partition --exclude=$exclude -N $node_qty $dir$execDir/generalRun.sh $dir $config_file $use_extrae $outFileIndex
  done
done
echo "End"
