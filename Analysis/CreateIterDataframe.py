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
    T_US = 22
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
#            "Stage_Bytes", "Iters", "Asynch_Iters", "T_iter", "T_stages", "T_spawn", "T_US", "T_SR", "T_AR", "T_Malleability", "T_total"] #27

columnsL = ["NP", "NC", "Total_Stages", "Granularity", "SDR", "ADR", "DR", "Redistribution_Method", \
            "Redistribution_Strategy", "Spawn_Method", "Spawn_Strategy", "Is_Dynamic", "FactorS", "Dist", "Stage_Types", "Stage_Times", \
            "Stage_Bytes", "N_Parents", "Asynch_Iters", "T_iter", "T_stages"] #20


def copy_iteration(row, dataL_it, group, iteration, is_asynch):
  basic_indexes = [G_enum.TOTAL_STAGES.value, G_enum.GRANULARITY.value, \
          G_enum.STAGE_TYPES.value, G_enum.STAGE_TIMES.value, G_enum.STAGE_BYTES.value]
  basic_asynch = [G_enum.SDR.value, G_enum.ADR.value, G_enum.DR.value]
  array_asynch_group = [G_enum.RED_METHOD.value, G_enum.RED_STRATEGY.value, \
          G_enum.SPAWN_METHOD.value, G_enum.SPAWN_STRATEGY.value, G_enum.DIST.value]

  dataL_it[G_enum.FACTOR_S.value] = row[G_enum.FACTOR_S.value][group]
  dataL_it[G_enum.NP.value] = row[G_enum.GROUPS.value][group]

  dataL_it[G_enum.ASYNCH_ITERS.value] = is_asynch
  dataL_it[G_enum.T_ITER.value] = row[G_enum.T_ITER.value][group][iteration]
  if type(row[G_enum.T_STAGES.value][group][0]) == tuple:
    dataL_it[G_enum.T_STAGES.value] = list(row[G_enum.T_STAGES.value][group][iteration])
  else:
    dataL_it[G_enum.T_STAGES.value] = [0]
  dataL_it[G_enum.IS_DYNAMIC.value] = True if group > 0 else False

  for index in basic_indexes:
    dataL_it[index] = row[index]

  for index in array_asynch_group:
    dataL_it[index] = [None, -1]
    dataL_it[index][0] = row[index][group]

  dataL_it[G_enum.N_PARENTS.value] = -1
  if group > 0:
    dataL_it[G_enum.N_PARENTS.value] = row[G_enum.GROUPS.value][group-1]

  if is_asynch:
    dataL_it[G_enum.NC.value] = row[G_enum.GROUPS.value][group+1]

    for index in basic_asynch:
      dataL_it[index] = row[index]
    for index in array_asynch_group:
      dataL_it[index][1] = row[index][group+1]

  for index in array_asynch_group: # Convert to tuple
    dataL_it[index] = tuple(dataL_it[index])

#-----------------------------------------------
def write_iter_dataframe(dataL, name, i, first=False):
  dfL = pd.DataFrame(dataL, columns=columnsL)
  dfL.to_pickle(name + str(i) + '.pkl')
  if first:
    print(dfL)

#-----------------------------------------------

def create_iter_dataframe(dfG, name, max_it_L):
  it = -1
  file_i = 0
  first = True
  dataL = []

  for row_index in range(len(dfG)):
    row = dfG.iloc[row_index]
    groups = row[G_enum.TOTAL_GROUPS.value]

    for group in range(groups):
        real_iterations = len(row[G_enum.T_ITER.value][group])
        real_asynch = row[G_enum.ASYNCH_ITERS.value][group]
        is_asynch = False
        for iteration in range(real_iterations-real_asynch):
            it += 1
            dataL.append( [None] * len(columnsL) )
            copy_iteration(row, dataL[it], group, iteration, is_asynch)
        is_asynch = True
        for iteration in range(real_iterations-real_asynch, real_iterations):
            it += 1
            dataL.append( [None] * len(columnsL) )
            copy_iteration(row, dataL[it], group, iteration, is_asynch)
    if it >= max_it_L-1: #Var "it" starts at -1, so one more must be extracted for precise cut
        write_iter_dataframe(dataL, name, file_i, first)
        dataL = []
        file_i += 1
        first = False
        it = -1

  if it != -1:
    write_iter_dataframe(dataL, name, file_i)



#-----------------------------------------------

if len(sys.argv) < 2:
    print("The files name is missing\nUsage: python3 CreateIterDataframe.py input_file.pkl output_name [max_rows_per_file]")
    exit(1)

input_name = sys.argv[1]
if len(sys.argv) > 2:
  name = sys.argv[2]
else:
  name = "dataL"
print("File names will be: " + name + ".pkl")

if len(sys.argv) > 3:
  max_it_L = int(sys.argv[3])
else:
  max_it_L = 100000

dfG = pd.read_pickle(input_name)
print(dfG)
create_iter_dataframe(dfG, name, max_it_L)

