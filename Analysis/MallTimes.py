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
    RED_METHOD = 7
    RED_STRATEGY = 8
    SPAWN_METHOD = 9
    SPAWN_STRATEGY = 10
    GROUPS = 11
    FACTOR_S = 12
    DIST = 13
    STAGE_TYPES = 14
    STAGE_TIMES = 15
    STAGE_BYTES = 16
    ITERS = 17
    ASYNCH_ITERS = 18
    T_ITER = 19
    T_STAGES = 20
    T_SPAWN = 21
    T_SPAWN_REAL = 22
    T_SR = 23
    T_AR = 24
    T_TOTAL = 25
    #Malleability specific
    NP = 0
    NC = 1
    BAR = 11 # Extract 1 from index


columnsG = ["Total_Resizes", "Total_Groups", "Total_Stages", "Granularity", "SDR", "ADR", "DR", "Redistribution_Method", \
            "Redistribution_Strategy", "Spawn_Method", "Spawn_Strategy", "Groups", "FactorS", "Dist", "Stage_Types", "Stage_Times", \
            "Stage_Bytes", "Iters", "Asynch_Iters", "T_iter", "T_stages", "T_spawn", "T_spawn_real", "T_SR", "T_AR", "T_total"] #26

columnsM = ["NP", "NC", "Total_Stages", "Granularity", "SDR", "ADR", "DR", "Redistribution_Method", \
            "Redistribution_Strategy", "Spawn_Method", "Spawn_Strategy", "FactorS", "Dist", "Stage_Types", "Stage_Times", \
            "Stage_Bytes", "Iters", "Asynch_Iters", "T_iter", "T_stages", "T_spawn", "T_spawn_real", "T_SR", "T_AR"] #24

# Obtains the value of a given index in a splited line
# and returns it as a float values if possible, string otherwise
def get_value(line, index):
  value = line[index].split('=')[1].split(',')[0]
  try:
    return float(value)
  except ValueError:
    return value

# Obtains the general parameters of an execution and
# stores them for creating a global dataframe
def record_config_line(lineS, dataG_it):
  ordered_indexes = [G_enum.TOTAL_RESIZES.value, G_enum.TOTAL_STAGES.value, \
          G_enum.GRANULARITY.value, G_enum.SDR.value, G_enum.ADR.value]
  offset_line = 2
  for i in range(len(ordered_indexes)):
    value = get_value(lineS, i+offset_line)
    if value.is_integer():
      value = int(value)
    index = ordered_indexes[i]
    dataG_it[index] = value

  dataG_it[G_enum.TOTAL_GROUPS.value] = dataG_it[G_enum.TOTAL_RESIZES.value]+1

  #FIXME Modificar cuando ADR ya no sea un porcentaje
  dataG_it[G_enum.DR.value] = dataG_it[G_enum.SDR.value] + dataG_it[G_enum.ADR.value]

  # Init lists for each column
  array_groups = [G_enum.GROUPS.value, G_enum.FACTOR_S.value, G_enum.DIST.value, G_enum.ITERS.value, \
          G_enum.ASYNCH_ITERS.value, G_enum.T_ITER.value, G_enum.T_STAGES.value]
  array_resizes = [G_enum.REDISTRIBUTION_METHOD.value, G_enum.REDISTRIBUTION_METHOD.value,
          G_enum.SPAWN_METHOD.value, G_enum.SPAWN_STRATEGY.value, G_enum.T_SPAWN.value, \
          G_enum.T_SPAWN_REAL.value, G_enum.T_SR.value, G_enum.T_AR.value]
  array_stages = [G_enum.STAGE_TYPES.value, \
          G_enum.STAGE_TIMES.value, G_enum.STAGE_BYTES.value]
  for index in array_groups:
    dataG_it[index] = [None]*dataG_it[G_enum.TOTAL_GROUPS.value]

  for index in array_resizes:
    dataG_it[index] = [None]*dataG_it[G_enum.TOTAL_RESIZES.value]

  for index in array_stages:
    dataG_it[index] = [None]*dataG_it[G_enum.TOTAL_STAGES.value]

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
    index = array_stages[i]
    dataG_it[index][stage] = value

# Obtains the parameters of a resize line
# and stores them in the dataframe
# Is needed to indicate to which group refers
# the resize line
def record_resize_line(lineS, dataG_it, group):
  array_groups = [G_enum.ITERS.value, G_enum.GROUPS.value, G_enum.FACTOR_S.value, G_enum.DIST.value, \
          G_enum.REDISTRIBUTION_METHOD.value, G_enum.REDISTRIBUTION_STRATEGY.value, G_enum.SPAWN_METHOD.value, G_enum.SPAWN_STRATEGY.value]
  offset_lines = 2
  for i in range(len(array_groups)):
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
  index = T_values[index]
  offset_lines = 1
  for i in range(len(dataG_it[index])):
    dataG_it[index][i] = get_value(lineS, i+offset_lines)

def record_multiple_times_line(lineS, dataG_it, ):
  T_values = [G_enum.T_SPAWN.value, G_enum.T_SPAWN_REAL.value, G_enum.T_SR.value, G_enum.T_AR.value, G_enum.T_TOTAL.value]
  if not (lineS[0] in T_names): # Execute only if line represents a Time
      return

  groups = dataG_it[G_enum.TOTAL_GROUPS.value]
  index = T_names.index(linesS[0])
  index = T_values[index]
  offset_lines = 1
  for i in range(len(dataG_it[index])):
  
  
#-----------------------------------------------
def read_local_file(f, dataG, it):
  resizes = 0
  timer = 0
  previousNP = 0

  for line in f: 
    lineS = line.split()

    if len(lineS) > 0:
      if lineS[0] == "Config": # CONFIG LINE
        it += 1
        record_config(lineS, dataG[it], dataM[it])
        resize = 0
        stage = 0

      elif lineS[0] == "Stage":
        record_stage_line(lineS, dataG[it], stage)
        stage+=1
      elif lineS[0] == "Resize":
        record_resize_line(lineS, dataG[it], resize)
        resize+=1
      elif lineS[0] == "T_total:":
        value = get_value(lineS, 1)
        dataG[it][G_enum.T_TOTAL.value] = value
      else:
        record_time_line(lineS, dataG[it])
          
  return it

#-----------------------------------------------
def read_global_file(f, dataG, it):
  run = -1

  for line in f: 
    lineS = line.split()

    if len(lineS) > 0:
      if lineS[0] == "Config": # CONFIG LINE
        it += 1
        nonlocal columnsG
        dataG.append([None]*len(columnsG))
        record_config(lineS, dataG[it])
        resize = 0
        stage = 0
        run += 1

      elif lineS[0] == "Stage":
        record_stage_line(lineS, dataG[it], stage)
        stage+=1
      elif lineS[0] == "Resize":
        record_resize_line(lineS, dataG[it], resize)
        resize+=1
      else:
        record_time_line(lineS, dataG[it])

  
  read_local_file(dataG[it])
          
  return it

#-----------------------------------------------
if len(sys.argv) < 2:
    print("The files name is missing\nUsage: python3 MallTimes.py resultsName directory csvOutName")
    exit(1)

if len(sys.argv) >= 3:
    BaseDir = sys.argv[2]
    print("Searching in directory: "+ BaseDir)
else:
    BaseDir = "./"

if len(sys.argv) >= 4:
  name = sys.argv[3]
else:
  name = "data"
print("Csv name will be: " + name + "G.csv & " + name + "M.csv")

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
dfM.loc[cond, ['TC', 'TH']] = dfM.loc[cond, ['TH', 'TC']].values
dfM.to_csv(name + 'M.csv')
