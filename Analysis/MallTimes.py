import sys
import glob
import numpy as np
import pandas as pd

def getData(lineS, outData, tp, hasIter = False):
  for data in lineS:
    k_v = data.split('=')
    if k_v[0] == "time":
      time = float(k_v[1])
    elif k_v[0] == "iters" and hasIter:
      iters = int(k_v[1])

  outData[tp] = time
  if hasIter:
    outData[tp+1] = iters

#-----------------------------------------------
def record(f, observation, line):
  # Record first line - General info
  lineS = line.split()
  for j in range(1,7):
    observation[j] = int(lineS[j].split('=')[1])

  # Record procces number
  line = next(f)
  lineS = line.split()
  j = 7
  for key_values in lineS:
    k_v = key_values.split('=')
    observation[j] = int(k_v[1])
    j+=1

  # Record data
  j = 9
  for j in range(9, 13):
    line = next(f)
    lineS = line.split()  
    getData(lineS, observation, j)

  line = next(f)
  lineS = line.split()  
  #if observation[0] == "A":
  getData(lineS, observation, 13, True)
  #else:
   # getData(lineS, observation, 13)


#-----------------------------------------------
def read_file(f, dataA, dataB, it):
  recording = False
  resizes = 0
  timer = 0
  previousNP = 0

  for line in f: 
    lineS = line.split()

    if len(lineS) > 0:
      if lineS[0] == "Config": # CONFIG LINE
        recording = True
        it += 1
        dataA.append([None]*9)
        dataB.append([None]*13)
        resizes = int(lineS[2].split('=')[1].split(',')[0])
        compute_tam = int(lineS[3].split('=')[1].split(',')[0])
        comm_tam = int(lineS[4].split('=')[1].split(',')[0])
        sdr = int(lineS[5].split('=')[1].split(',')[0])
        adr = int(lineS[6].split('=')[1].split(',')[0]) #TODO Que lo tome como porcentaje
        # TODO Que obtenga Aib
        time = float(lineS[8].split('=')[1])

        dataB[it][0] = sdr
        dataB[it][1] = adr 
        dataB[it][4] = "" 
        dataB[it][5] = compute_tam
        dataB[it][6] = comm_tam
        dataB[it][7] = time
        dataB[it][8] = "" 

        dataA[it][0] = sdr
        dataA[it][1] = adr 
        dataA[it][3] = ""
        dataA[it][4] = compute_tam
        dataA[it][5] = comm_tam
        dataA[it][6] = time
        dataA[it][7] = ""

      elif recording and resizes != 0: # RESIZE LINE
        iters = int(lineS[2].split('=')[1].split(',')[0])
        npr = int(lineS[3].split('=')[1].split(',')[0])
        dist = lineS[5].split('=')[1]

        resizes = resizes - 1
        if resizes == 0:
          dataB[it][3] = npr
          dataB[it][4] += dist
          dataB[it][8] += str(iters)

          dataA[it][2] = str(previousNP) + "," + str(npr)
          dataA[it][3] += dist
          dataA[it][7] += str(iters)
          timer = 4
        else:
          dataB[it][2] = npr
          dataB[it][4] += dist + ","
          dataB[it][8] += str(iters) + ","

          dataA[it][3] += dist + ","
          dataA[it][7] += str(iters) + ","
          previousNP = npr

      else: # SAVE TIMES
        if timer == 4:
          dataB[it][9] = float(lineS[1])
        elif timer == 3:
          dataB[it][10] = float(lineS[1])
        elif timer == 2:
          dataB[it][11] = float(lineS[1])
        elif timer == 1:
          dataB[it][12] = float(lineS[1])
        else:
          dataA[it][8] = float(lineS[1])
        timer = timer - 1
          
  return it
#columnsA1 = ["N", "%Async", "Groups", "Dist", "Matrix", "CommTam", "Time", "Iters", "TE"] #8
#columnsB1 = ["N", "%Async", "NP", "NS", "Dist", "Matrix", "CommTam", "Time", "Iters", "TC", "TS", "TA"] #12
#Config loaded: resizes=2, matrix=1000, sdr=1000000000, adr=0, aib=0, time=2.000000 || grp=1
#Resize 0: Iters=100, Procs=2, Factors=1.000000, Phy=2
#Resize 1: Iters=100, Procs=4, Factors=0.500000, Phy=2
#Tspawn: 0.249393 
#Tsync: 0.330391 
#Tasync: 0
#Tex: 301.428615
#-----------------------------------------------

if len(sys.argv) < 2:
    print("The files name is missing\nUsage: python3 iterTimes.py resultsName directory csvOutName")
    exit(1)

if len(sys.argv) >= 3:
    BaseDir = sys.argv[2]
    print("Searching in directory: "+ BaseDir)
else:
    BaseDir = sys.argv[2]

if len(sys.argv) >= 4:
  print("Csv name will be: " + sys.argv[3] + "G.csv & " + sys.argv[3] + "M.csv")
  name = sys.argv[3]
else:
  name = "data"

insideDir = "Run"
lista = glob.glob("./" + BaseDir + insideDir + "*/" + sys.argv[1]+ "*Global.o*")
print("Number of files found: "+ str(len(lista)));

it = -1
dataA = []
dataB = []
columnsA = ["N", "%Async", "Groups", "Dist", "Matrix", "CommTam", "Time", "Iters", "TE"] #9
columnsB = ["N", "%Async", "NP", "NS", "Dist", "Matrix", "CommTam", "Time", "Iters", "TC", "TH", "TS", "TA"] #13

for elem in lista:
  f = open(elem, "r")
  it = read_file(f, dataA, dataB, it)
  f.close()

#print(data)
dfA = pd.DataFrame(dataA, columns=columnsA)
dfA.to_csv(name + 'G.csv')

dfB = pd.DataFrame(dataB, columns=columnsB)
dfB.to_csv(name + 'M.csv')
