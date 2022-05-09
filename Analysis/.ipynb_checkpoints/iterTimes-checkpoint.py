import sys
import glob
import numpy as np
import pandas as pd

#-----------------------------------------------
def read_file(f, data, it):
  matrix = 0
  sdr = 0
  adr = 0
  time = 0
  recording = False
  it_line = 0
  aux_it = 0
  iters = 0
  np = 0
  np_par = 0
  ns = 0

  for line in f: 
    lineS = line.split()

    if len(lineS) > 1:
        if recording:
          aux_it = 0
          if it_line==0:
            lineS.pop(0)
            for observation in lineS:
              data.append([None]*11)
              data[it+aux_it][0] = sdr
              data[it+aux_it][1] = adr 
              data[it+aux_it][2] = np
              data[it+aux_it][3] = np_par
              data[it+aux_it][4] = ns
              data[it+aux_it][5] = matrix
              data[it+aux_it][6] = time
              data[it+aux_it][7] = iters
              data[it+aux_it][8] = float(observation)
              aux_it+=1
            it_line = it_line + 1

          elif it_line==1:
            lineS.pop(0)
            for observation in lineS:
              data[it+aux_it][9] = float(observation)           
              aux_it+=1
            it_line = it_line + 1
          else:
            lineS.pop(0)
            for observation in lineS:
              data[it+aux_it][10] = float(observation)
              aux_it+=1
            it = it + aux_it
            recording = False
            it_line = 0

#TODO Que tome adr como porcentaje
        if lineS[0] == "Config:":
          matrix = int(lineS[1].split('=')[1].split(',')[0])
          sdr = int(lineS[2].split('=')[1].split(',')[0])
          adr = int(lineS[3].split('=')[1].split(',')[0]) 
          time = float(lineS[5].split('=')[1])
        elif lineS[0] == "Config":
          recording = True
          iters = int(lineS[2].split('=')[1].split(',')[0])
          np = int(lineS[5].split('=')[1].split(',')[0])
          np_par = int(lineS[6].split('=')[1].split(',')[0])
          ns = int(float(lineS[7].split('=')[1]))

  return it
#-----------------------------------------------
#Config: matrix=1000, sdr=1000000000, adr=0, aib=0 time=2.000000
#Config Group: iters=100, factor=1.000000, phy=2, procs=2, parents=0, sons=4
#Ttype: 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 

if len(sys.argv) < 2:
    print("The files name is missing\nUsage: python3 iterTimes.py resultsName directory csvOutName")
    exit(1)

if len(sys.argv) >= 3:
    BaseDir = sys.argv[2]
    print("Searching in directory: "+ BaseDir)
else:
    BaseDir = sys.argv[2]

if len(sys.argv) >= 4:
  print("Csv name will be: " + sys.argv[3] + ".csv")
  name = sys.argv[3]
else:
  name = "data"

insideDir = "Run"
lista = glob.glob("./" + BaseDir + insideDir + "*/" + sys.argv[1]+ "*ID*.o*")
print("Number of files found: "+ str(len(lista)));

it = 0
data = []  #0   #1        #2    #3       #4    #5        #6      #7       #8    #9    #10
columns = ["N", "%Async", "NP", "N_par", "NS", "Matrix", "Time", "Iters", "Ti", "Tt", "To"] #11

for elem in lista:
  f = open(elem, "r")
  it = read_file(f, data, it)
  f.close()

#print(data)
df = pd.DataFrame(data, columns=columns)
df.to_csv(name + '.csv')
