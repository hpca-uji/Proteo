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
    T_MALLEABILITY = 25
    T_TOTAL = 26
    #Malleability specific
    NP = 0
    NC = 1
    #Iteration specific
    IS_DYNAMIC = 11
    N_PARENTS = 17

#columnsG = ["Total_Resizes", "Total_Groups", "Total_Stages", "Granularity", "SDR", "ADR", "DR", "Redistribution_Method", \
#            "Redistribution_Strategy", "Spawn_Method", "Spawn_Strategy", "Groups", "FactorS", "Dist", "Stage_Types", "Stage_Times", \
#            "Stage_Bytes", "Iters", "Asynch_Iters", "T_iter", "T_stages", "T_spawn", "T_spawn_real", "T_SR", "T_AR", "T_Malleability", "T_total"] #27

columnsM = ["NP", "NC", "Total_Stages", "Granularity", "SDR", "ADR", "DR", "Redistribution_Method", \
            "Redistribution_Strategy", "Spawn_Method", "Spawn_Strategy", "FactorS", "Dist", "Stage_Type", "Stage_Time", \
            "Stage_Bytes", "Iters", "Asynch_Iters", "T_iter", "T_stages", "T_spawn", "T_spawn_real", "T_SR", "T_AR", "T_Malleability"] #25

def copy_resize(row, dataM_it, resize):
  basic_indexes = [G_enum.TOTAL_STAGES.value, G_enum.GRANULARITY.value, G_enum.SDR.value, \
          G_enum.ADR.value, G_enum.DR.value]
  basic_group = [G_enum.STAGE_TYPES.value, G_enum.STAGE_TIMES.value, G_enum.STAGE_BYTES.value]
  array_actual_group = [G_enum.FACTOR_S.value, G_enum.ITERS.value, G_enum.ASYNCH_ITERS.value, \
          G_enum.T_SPAWN.value, G_enum.T_SPAWN_REAL.value, G_enum.T_SR.value, \
          G_enum.T_AR.value, G_enum.T_MALLEABILITY.value, G_enum.T_ITER.value, G_enum.T_STAGES.value]
  array_next_group = [G_enum.RED_METHOD.value, G_enum.RED_STRATEGY.value, \
          G_enum.SPAWN_METHOD.value, G_enum.SPAWN_STRATEGY.value]

  dataM_it[G_enum.NP.value] = row[G_enum.GROUPS.value][resize]
  dataM_it[G_enum.NC.value] = row[G_enum.GROUPS.value][resize+1]
  dataM_it[G_enum.DIST.value-1] = [None, None]
  dataM_it[G_enum.DIST.value-1][0] = row[G_enum.DIST.value][resize]
  dataM_it[G_enum.DIST.value-1][1] = row[G_enum.DIST.value][resize+1]

  for index in basic_indexes:
    dataM_it[index] = row[index]
    
  for index in basic_group:
    dataM_it[index-1] = row[index]

  for index in array_actual_group:
    dataM_it[index-1] = row[index][resize] 

  for index in array_next_group:
    dataM_it[index] = row[index][resize+1]


#-----------------------------------------------

def create_resize_dataframe(dfG, dataM):
  it = -1
  for row_index in range(len(dfG)):
    row = dfG.iloc[row_index]
    resizes = row[G_enum.TOTAL_RESIZES.value]

    for resize in range(resizes):
      it += 1
      dataM.append( [None] * len(columnsM) )
      copy_resize(row, dataM[it], resize)

#-----------------------------------------------
if len(sys.argv) < 2:
    print("The files name is missing\nUsage: python3 CreateResizeDataframe.py input_file.pkl output_name")
    exit(1)

input_name = sys.argv[1]
if len(sys.argv) > 2:
  name = sys.argv[2]
else:
  name = "dataM"
print("File name will be: " + name + ".pkl")


dfG = pd.read_pickle(input_name)
dataM = []
create_resize_dataframe(dfG, dataM)

dfM = pd.DataFrame(dataM, columns=columnsM)
dfM.to_pickle(name + '.pkl')

print(dfG)
print(dfM)
