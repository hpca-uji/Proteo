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
    T_USER = 22
    T_SR = 23
    T_AR = 24
    T_MALLEABILITY = 25
    T_TOTAL = 26
    #Malleability specific
    NP = 0
    NC = 1
    #Iteration specific
    IS_DYNAMIC = 11
    N_PARENTS = 17


columnsG = ["Total_Resizes", "Total_Groups", "Total_Stages", "Granularity", "SDR", "ADR", "DR", "Redistribution_Method", \
            "Redistribution_Strategy", "Spawn_Method", "Spawn_Strategy", "Groups", "FactorS", "Dist", "Stage_Types", "Stage_Times", \
            "Stage_Bytes", "Iters", "Asynch_Iters", "T_iter", "T_stages", "T_spawn", "T_US", "T_SR", "T_AR", "T_Malleability", "T_total"] #27

#-----------------------------------------------
# Obtains the value of a given index in a splited line
# and returns it as a float values if possible, string otherwise
def get_value(line, index, separator=True):
  if separator:
    value = line[index].split('=')[1].split(',')[0]
  else:
    value = line[index]

  try:
    value = float(value)
    if value.is_integer():
      value = int(value)
  except ValueError:
    return value
  return value

#-----------------------------------------------
# Obtains the general parameters of an execution and
# stores them for creating a global dataframe
def record_config_line(lineS, dataG_it):
  ordered_indexes = [G_enum.TOTAL_RESIZES.value, G_enum.TOTAL_STAGES.value, \
          G_enum.GRANULARITY.value, G_enum.SDR.value, G_enum.ADR.value]
  offset_line = 2
  for i in range(len(ordered_indexes)):
    value = get_value(lineS, i+offset_line)
    index = ordered_indexes[i]
    dataG_it[index] = value

  dataG_it[G_enum.TOTAL_GROUPS.value] = dataG_it[G_enum.TOTAL_RESIZES.value]+1

  #FIXME Modificar cuando ADR ya no sea un porcentaje
  dataG_it[G_enum.DR.value] = dataG_it[G_enum.SDR.value] + dataG_it[G_enum.ADR.value]

  # Init lists for each column
  array_groups = [G_enum.GROUPS.value, G_enum.FACTOR_S.value, G_enum.DIST.value, G_enum.ITERS.value, \
          G_enum.ASYNCH_ITERS.value, G_enum.T_ITER.value, G_enum.T_STAGES.value, G_enum.RED_METHOD.value, \
          G_enum.RED_STRATEGY.value, G_enum.SPAWN_METHOD.value, G_enum.SPAWN_STRATEGY.value,]
  array_resizes = [ G_enum.T_SPAWN.value, G_enum.T_USER.value, G_enum.T_SR.value, G_enum.T_AR.value, G_enum.T_MALLEABILITY.value]
  array_stages = [G_enum.STAGE_TYPES.value, \
          G_enum.STAGE_TIMES.value, G_enum.STAGE_BYTES.value]
  for index in array_groups:
    dataG_it[index] = [None]*dataG_it[G_enum.TOTAL_GROUPS.value]
  for group in range(dataG_it[G_enum.TOTAL_GROUPS.value]):
    dataG_it[G_enum.T_ITER.value][group] = []

  for index in array_resizes:
    dataG_it[index] = [None]*dataG_it[G_enum.TOTAL_RESIZES.value]

  for index in array_stages:
    dataG_it[index] = [None]*dataG_it[G_enum.TOTAL_STAGES.value]

#-----------------------------------------------
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
    index = array_stages[i]
    dataG_it[index][stage] = value

#-----------------------------------------------
# Obtains the parameters of a resize line
# and stores them in the dataframe
# Is needed to indicate to which group refers
# the resize line
# Group 0: Iters=3, Procs=80, Factors=0.037500, Dist=2, RM=0, SM=0, RS=0, SS=0
def record_group_line(lineS, dataG_it, group):
  array_groups = [G_enum.ITERS.value, G_enum.GROUPS.value, G_enum.FACTOR_S.value, G_enum.DIST.value, \
          G_enum.RED_METHOD.value, G_enum.SPAWN_METHOD.value, G_enum.RED_STRATEGY.value, G_enum.SPAWN_STRATEGY.value]
  offset_lines = 2
  for i in range(len(array_groups)):
    value = get_value(lineS, i+offset_lines)
    index = array_groups[i]
    dataG_it[index][group] = value

#-----------------------------------------------
def record_time_line(lineS, dataG_it):
  T_names = ["T_spawn:", "T_US:", "T_SR:", "T_AR:", "T_Malleability:", "T_total:"]
  T_values = [G_enum.T_SPAWN.value, G_enum.T_USER.value, G_enum.T_SR.value, G_enum.T_AR.value, G_enum.T_MALLEABILITY.value, G_enum.T_TOTAL.value]
  if not (lineS[0] in T_names): # Execute only if line represents a Time
      return

  index = T_names.index(lineS[0])
  index = T_values[index]
  offset_lines = 1

  len_index = 1
  if dataG_it[index] != None:
    len_index = len(dataG_it[index])
    for i in range(len_index):
      dataG_it[index][i] = get_value(lineS, i+offset_lines, False)
  else:
      dataG_it[index] = get_value(lineS, offset_lines, False)

#-----------------------------------------------
def record_multiple_times_line(lineS, dataG_it, group):
  T_names = ["T_iter:", "T_stage"]
  T_values = [G_enum.T_ITER.value, G_enum.T_STAGES.value]
  if not (lineS[0] in T_names): # Execute only if line represents a Time
      return

  index = T_names.index(lineS[0])
  index = T_values[index]

  offset_lines = 1
  if index == G_enum.T_STAGES.value:
    offset_lines += 1
    total_iters = len(lineS)-offset_lines
    stage = int(lineS[1].split(":")[0])
    if stage == 0:
      dataG_it[index][group] = [None] * total_iters
      for i in range(total_iters):
        dataG_it[index][group][i] = [None] * dataG_it[G_enum.TOTAL_STAGES.value]
    for i in range(total_iters):
        dataG_it[index][group][i][stage] = get_value(lineS, i+offset_lines, False)
  else:
    total_iters = len(lineS)-offset_lines
    for i in range(total_iters):
      dataG_it[index][group].append(get_value(lineS, i+offset_lines, False))
  
#-----------------------------------------------
def read_local_file(f, dataG, it, runs_in_file):
  offset = 0
  real_it = 0
  group = 0

  for line in f:
    lineS = line.split()

    if len(lineS) > 0:
      if lineS[0] == "Group": # GROUP number
        offset += 1
        real_it = it - (runs_in_file-offset)
        group = int(lineS[1].split(":")[0])
      elif lineS[0] == "Async_Iters:":
        offset_line = 1
        dataG[real_it][G_enum.ASYNCH_ITERS.value][group] = get_value(lineS, offset_line, False)
      else:
        record_multiple_times_line(lineS, dataG[real_it], group)

#-----------------------------------------------
def read_global_file(f, dataG, it):
  runs_in_file=0
  for line in f: 
    lineS = line.split()

    if len(lineS) > 0:
      if lineS[0] == "Config": # CONFIG LINE
        it += 1
        runs_in_file += 1
        group = 0
        stage = 0

        dataG.append([None]*len(columnsG))
        record_config_line(lineS, dataG[it])

      elif lineS[0] == "Stage":
        record_stage_line(lineS, dataG[it], stage)
        stage+=1
      elif lineS[0] == "Group":
        record_group_line(lineS, dataG[it], group)
        group+=1
      else:
        record_time_line(lineS, dataG[it])

  return it,runs_in_file

#-----------------------------------------------


#-----------------------------------------------
def convert_to_tuples(dfG):
  array_list_items = [G_enum.GROUPS.value, G_enum.FACTOR_S.value, G_enum.DIST.value, G_enum.ITERS.value, \
          G_enum.ASYNCH_ITERS.value, G_enum.RED_METHOD.value, G_enum.RED_STRATEGY.value, G_enum.SPAWN_METHOD.value, \
          G_enum.SPAWN_STRATEGY.value, G_enum.T_SPAWN.value, G_enum.T_USER.value, G_enum.T_SR.value, \
          G_enum.T_AR.value, G_enum.STAGE_TYPES.value, G_enum.STAGE_TIMES.value, G_enum.STAGE_BYTES.value]
            #TODO Falta T_malleability?
  array_multiple_list_items = [G_enum.T_ITER.value, G_enum.T_STAGES.value]
  for item in array_list_items:
    name = columnsG[item]
    values = dfG[name].copy()
    for index in range(len(values)):
      values[index] = tuple(values[index])
    dfG[name] = values

  for item in array_multiple_list_items:
    name = columnsG[item]
    values = dfG[name].copy()
    for i in range(len(values)):
      for j in range(len(values[i])):
        if(len(values[i][j]) == 0):
          values[i][j].append(0) # Add one element in case is empty
        if(type(values[i][j][0]) == list):
          for r in range(len(values[i][j])):
            values[i][j][r] = tuple(values[i][j][r])
        values[i][j] = tuple(values[i][j])
      values[i] = tuple(values[i])
    dfG[name] = values

#-----------------------------------------------

if len(sys.argv) < 2:
    print("The files name is missing\nUsage: python3 MallTimes.py commonName directory OutName")
    exit(1)

common_name = sys.argv[1]
if len(sys.argv) >= 3:
    BaseDir = sys.argv[2]
    print("Searching in directory: "+ BaseDir)
else:
    BaseDir = "./"

if len(sys.argv) >= 4:
  name = sys.argv[3]
else:
  name = "data"
print("File name will be: " + name + "G.pkl")

insideDir = "Run"
lista = glob.glob(BaseDir + insideDir + "*/" + common_name + "*_Global.out")
lista += (glob.glob(BaseDir + common_name + "*_Global.out")) # Se utiliza cuando solo hay un nivel de directorios
print("Number of files found: "+ str(len(lista)));

it = -1
dataG = []

for elem in lista:
  f = open(elem, "r")
  id_run = elem.split("_Global.out")[0].split(common_name)[-1] 
  lista_local = glob.glob(BaseDir + common_name + id_run + "_G*NP*.out")

  it,runs_in_file = read_global_file(f, dataG, it)
  f.close()
  for elem_local in lista_local:
    f_local = open(elem_local, "r")
    read_local_file(f_local, dataG, it, runs_in_file)
    f_local.close()


dfG = pd.DataFrame(dataG, columns=columnsG)
convert_to_tuples(dfG)
print(dfG)
dfG.to_pickle(name + 'G.pkl')

#dfM = pd.DataFrame(dataM, columns=columnsM)

#Poner en TC el valor real y en TH el necesario para la app
#cond = dfM.TH != 0
#dfM.loc[cond, ['TC', 'TH']] = dfM.loc[cond, ['TH', 'TC']].values
#dfM.to_csv(name + 'M.csv')
