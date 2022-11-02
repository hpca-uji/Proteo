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

# Obtains the value of a given index in a splited line
# and returns it as a float values
def get_value(line, index):
  return float(line[index].split('=')[1].split(',')[0])

# Obtains the general parameters of an execution and
# stores them for creating a dataframe
def record_config_line(lineS, dataA, dataB):
  dataA.append([None]*13)
  dataB.append([None]*15)
  resizes = int(get_value(lineS, 2))
  stages = int(get_value(lineS, 3))
  compute_tam = int(get_value(lineS, 4))
  sdr = int(get_value(lineS, 5))
  adr = int(get_value(lineS, 6)) #TODO Que lo tome como porcentaje
  at  = int(get_value(lineS, 7))
  sm = int(get_value(lineS, 8))
  ss = int(get_value(lineS, 9))
  latency = get_value(lineS, 10)
  bw = get_value(lineS, 11)

  dataB[it][0] = sdr
  dataB[it][1] = adr 
  dataB[it][4] = "" 
  dataB[it][5] = compute_tam
  dataB[it][6] = comm_tam
  dataB[it][7] = cst
  dataB[it][8] = css
  dataB[it][9] = time
  dataB[it][10] = "" 

  dataA[it][0] = sdr
  dataA[it][1] = adr 
  dataA[it][5] = ""
  dataA[it][6] = compute_tam
  dataA[it][7] = comm_tam
  dataA[it][8] = cst
  dataA[it][9] = css
  dataA[it][10] = time
  dataA[it][11] = ""

def record_stage_line(lineS, dataG_it, dataM_it):
  pt = int(get_value(lineS, 2))
  t_stage = get_value(lineS, 3)
  u_bytes = int(get_value(lineS, 4))

  dataG_it[].append(pt)
  dataG_it[].append(t_stage)
  dataG_it[].append(u_bytes)

  dataM_it[].append(pt)
  dataM_it[].append(t_stage)
  dataM_it[].append(u_bytes)
  
def record_resize_line(lineS, dataG_it, dataM_it):
        iters = int(lineS[2].split('=')[1].split(',')[0])
        npr = int(lineS[3].split('=')[1].split(',')[0])
        dist = lineS[5].split('=')[1]

        resizes = resizes - 1
        if resizes == 0:
          dataB[it][3] = npr
          dataB[it][4] += dist
          dataB[it][10] += str(iters)

          dataA[it][4] = npr #FIXME No sera correcta si hay mas de una reconfig
          dataA[it][2] = str(previousNP) + "," + str(npr)
          dataA[it][5] += dist
          dataA[it][11] += str(iters)
          timer = 4
        else:
          dataB[it][2] = npr
          dataB[it][4] += dist + ","
          dataB[it][10] += str(iters) + ","

          dataA[it][3] = npr
          dataA[it][5] += dist + ","
          dataA[it][11] += str(iters) + ","
          previousNP = npr

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
        record_config(lineS, dataG, dataM)

      elif lineS[0] == "Stage":
          record_stage_line(lineS, dataG, dataM)
      elif lineS[0] == "Resize":
      elif recording and resizes != 0: # RESIZE LINE
        iters = int(lineS[2].split('=')[1].split(',')[0])
        npr = int(lineS[3].split('=')[1].split(',')[0])
        dist = lineS[5].split('=')[1]

        resizes = resizes - 1
        if resizes == 0:
          dataB[it][3] = npr
          dataB[it][4] += dist
          dataB[it][10] += str(iters)

          dataA[it][4] = npr #FIXME No sera correcta si hay mas de una reconfig
          dataA[it][2] = str(previousNP) + "," + str(npr)
          dataA[it][5] += dist
          dataA[it][11] += str(iters)
          timer = 4
        else:
          dataB[it][2] = npr
          dataB[it][4] += dist + ","
          dataB[it][10] += str(iters) + ","

          dataA[it][3] = npr
          dataA[it][5] += dist + ","
          dataA[it][11] += str(iters) + ","
          previousNP = npr

      else: # SAVE TIMES
        if timer == 4:
          dataB[it][11] = float(lineS[1])
        elif timer == 3:
          dataB[it][12] = float(lineS[1])
        elif timer == 2:
          dataB[it][13] = float(lineS[1])
        elif timer == 1:
          dataB[it][14] = float(lineS[1])
        else:
          dataA[it][12] = float(lineS[1])
        timer = timer - 1
          
  return it
#columnsA1 = ["N", "%Async", "Groups", "Dist", "Matrix", "CommTam", "Cst", "Css", "Time", "Iters", "TE"] #8
#columnsB1 = ["N", "%Async", "NP", "NS", "Dist", "Matrix", "CommTam", "Cst", "Css", "Time", "Iters", "TC", "TS", "TA"] #12
#Config loaded: resizes=2, matrix=1000, sdr=1000000000, adr=0, aib=0, time=2.000000 || grp=1
#Resize 0: Iters=100, Procs=2, Factors=1.000000, Phy=2
#Resize 1: Iters=100, Procs=4, Factors=0.500000, Phy=2
#Tspawn: 0.249393 
#Tthread: 0 
#Tsync: 0.330391 
#Tasync: 0
#Tex: 301.428615

#Config loaded: resizes=1, matrix=0, comm_tam=0, sdr=0, adr=0, aib=0, cst=3, css=1, time=1 || grp=1
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
lista += (glob.glob("./" + BaseDir + sys.argv[1]+ "*Global.o*")) # Se utiliza cuando solo hay un nivel de directorios
print("Number of files found: "+ str(len(lista)));

it = -1
dataG = []
dataM = []
columnsG = ["N", "%Async", "Groups", "NP", "NS", "Dist", "Matrix", "CommTam", "Cst", "Css", "Time", "Iters", "TE"] #13
columnsM = ["N", "%Async", "NP", "NS", "Dist", "Matrix", "CommTam", "Cst", "Css", "Time", "Iters", "TC", "TH", "TS", "TA"] #15

for elem in lista:
  f = open(elem, "r")
  it = read_file(f, dataG, dataM, it)
  f.close()

#print(data)
dfG = pd.DataFrame(dataG, columns=columnsG)
dfG.to_csv(name + 'G.csv')

dfM = pd.DataFrame(dataM, columns=columnsM)

#Poner en TC el valor real y en TH el necesario para la app
cond = dfM.TH != 0
dfM.loc[cond, ['TC', 'TH']] = dfB.loc[cond, ['TH', 'TC']].values
dfM.to_csv(name + 'M.csv')
