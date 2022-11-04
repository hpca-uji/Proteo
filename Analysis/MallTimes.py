import sys
import glob
import numpy as np
import pandas as pd
from enum import Enum

class G_enum(Enum):
    TOTAL_RESIZES = 0
    TOTAL_GROUPS = 1
    TOTAL_STAGES = 2
    GRANULARITY = 3
    SDR = 4
    ADR = 5
    DR = 6
    ASYNCH_REDISTRIBUTION_TYPE = 7
    SPAWN_METHOD = 8
    SPAWN_STRATEGY = 9
    GROUPS = 10
    FACTOR_S = 11
    DIST = 12
    STAGE_TYPES = 13
    STAGE_TIMES = 14
    STAGE_BYTES = 15
    ITERS = 16
    ASYNCH_ITERS = 17
    T_ITER = 18
    T_STAGES = 19
    T_SPAWN = 20
    T_SPAWN_REAL = 21
    T_SR = 22
    T_AR = 23
    T_TOTAL = 24

columnsG = ["Total_Resizes", "Total_Groups", "Total_Stages", "Granularity", "SDR", "ADR", "DR", "Asynch_Redistribution_Type", \
            "Spawn_Method", "Spawn_Strategy", "Groups", "Factor_S", "Dist", "Stage_Types", "Stage_Times", "Stage_Bytes", \
            "Iters", "Asynch_Iters", "T_iter", "T_stages", "T_spawn", "T_spawn_real", "T_SR", "T_AR", "T_total"] #25

# Obtains the value of a given index in a splited line
# and returns it as a float values
def get_value(line, index):
  return float(line[index].split('=')[1].split(',')[0])

# Obtains the general parameters of an execution and
# stores them for creating a global dataframe
def record_config_line(lineS, dataG_it):
  ordered_indexes = [G_enum.TOTAL_RESIZES.value, G_enum.TOTAL_STAGES.value, G_enum.GRANULARITY.value, G_enum.SDR.value, \
          G_enum.ADR.value, G_enum.ASYNCH_REDISTRIBUTION_TYPE.value, G_enum.SPAWN_METHOD.value, G_emun.SPAWN_STRATEGY.value]
  offset_line = 2
  for i in range(len(ordered_indexes)):
    value = get_value(lineS, i+offset_line)
    if value.is_integer():
      value = int(value)
    index = ordered_indexes[i]
    dataG_it[index] = value

  dataG_it[G_enum.TOTAL_GROUPS.value] = dataG_it[G_enum.TOTAL_RESIZES.value]
  dataG_it[G_enum.TOTAL_RESIZES.value] -=1 #FIXME Modificar en App sintetica

  #FIXME Modificar cuando ADR ya no sea un porcentaje
  dataG_it[G_enum.DR.value] = dataG_it[G_enum.SDR.value] + dataG_it[G_enum.ADR.value]

  # Init lists for each column
  array_groups = [G_enum.GROUPS.value, G_enum.FACTOR_S.value, G_enum.DIST.value, G_enum.ITERS.value, \
          G_enum.ASYNCH_ITERS.value, G_enum.T_ITER.value, G_enum.T_STAGES.value]
  array_resizes = [G_enum.ASYNCH_REDISTRIBUTION_TYPE.value, G_enum.SPAWN_METHOD.value, \
          G_enum.SPAWN_STRATEGY.value, G_enum.T_SPAWN.value, G_enum.T_SPAWN_REAL.value, \
          G_enum.T_SR.value, G_enum.T_AR.value]
  array_stages = [G_enum.STAGE_TYPES.value, \
          G_enum.STAGE_TIMES.value, G_enum.STAGE_BYTES.value]
  for index in array_groups:
    dataG_it[index] = [None]*dataG_it[G_enum.TOTAL_GROUPS.value]

  for index in array_resizes:
    dataG_it[index] = [None]*dataG_it[G_enum.TOTAL_RESIZES.value]

  for index in array_stages:
    dataG_it[index] = [None]*dataG_it[G_enum.TOTAL_STAGES.value]





#columnsG = ["Total_Resizes", "Total_Groups", "Total_Stages", "Granularity", "SDR", "ADR", "DR", "Asynch_Redistribution_Type", \\
#            "Spawn_Method", "Spawn_Strategy", "Groups", "Dist",   "Stage_Types", "Stage_Times", "Stage_Bytes", \\
#            "Iters", "Asynch_Iters", "T_iter", "T_stages", "T_spawn", "T_spawn_real", "T_SR", "T_AR", "T_total"] #24
#columnsG = ["N", "%Async", "Groups", "NP", "NS", "Dist", "Matrix", "CommTam", "Cst", "Css", "Time", "Iters", "TE"] #13

# Obtains the parameters of a stage line 
# and stores it in the dataframe
# Is needed to indicate in which stage is
# being performed
def record_stage_line(lineS, dataG_it, stage):
  array_stages = [G_enum.STAGE_TYPES.value, \
          G_enum.STAGE_TIMES.value, G_enum.STAGE_BYTES.value]
  offset_lines = 2
  for i in range(len(array_stages)):
    value = get_value(lineS, i+offset_lines)
    if value.is_integer():
        value = int(value)
    index = array_stage[i]
    dataG_it[index][stage] = value

# Obtains the parameters of a resize line
# and stores them in the dataframe
# Is needed to indicate to which group refers
# the resize line
def record_resize_line(lineS, dataG_it, group):
  array_stages = [G_enum.ITERS.value, G_enum.GROUPS.value\
          G_enum.FACTOR_S.value, G_enum.DIST.value]
  offset_lines = 2
  for i in range(len(array_stages)):
    value = get_value(lineS, i+offset_lines)
    if value.is_integer():
        value = int(value)
    index = array_stage[i]
    dataG_it[index][group] = value

def record_time_line(lineS, dataG_it):
  T_names = ["T_spawn:", "T_spawn_real:", "T_SR:", "T_AR:", "T_total:"]
  T_values = [G_enum.T_SPAWN.value, G_enum.T_SPAWN_REAL.value, G_enum.T_SR.value, G_enum.T_AR.value, G_enum.T_TOTAL.value]
  if not (lineS[0] in T_names): # Execute only if line represents a Time
      return

  index = T_names.index(linesS[0])
  offset_lines = 1
  for i in range(len(dataG_it[index])):
    value = get_value(lineS, i+offset_lines)
    dataG_it[index][i] = value

#-----------------------------------------------
def read_global_file(f, dataA, dataB, it):
  resizes = 0
  timer = 0
  previousNP = 0

  for line in f: 
    lineS = line.split()

    if len(lineS) > 0:
      if lineS[0] == "Config": # CONFIG LINE
        it += 1
        dataA.append([None]*25)
        record_config(lineS, dataG[it], dataM[it])

      elif lineS[0] == "Stage":
        record_stage_line(lineS, dataG[it], ??)
      elif lineS[0] == "Resize":
        record_resize_line(lineS, dataG[it], ??)
      elif lineS[0] in T_names:
        dataG[it][]
          
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
