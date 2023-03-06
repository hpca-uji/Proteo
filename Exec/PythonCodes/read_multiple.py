import sys
import glob
import os
from datetime import date
from enum import Enum

GENERAL_SECTION = "[general]"
RESIZE_SECTION = "[resize"
STAGE_SECTION = "[stage"
END_SECTION_DELIMITER = ";end"

class Config_section(Enum):
    INVALID=0
    GENERAL=1
    RESIZE=2
    STAGE=3

    P_TOTAL_RESIZES="Total_Resizes"
    P_TOTAL_STAGES="Total_Stages"
    P_GRANULARITY="Granularity"
    P_SDR="SDR"
    P_ADR="ADR"
    P_RIGID="Rigid"

    P_STAGE_TYPE="Stage_Type"
    P_STAGE_BYTES="Stage_Bytes"
    P_STAGE_TIME_CAPPED="Stage_Time_Capped"
    P_STAGE_TIME="Stage_Time"

    P_RESIZE_ITERS="Iters"
    P_RESIZE_PROCS="Procs"
    P_RESIZE_FACTORS="FactorS"
    P_RESIZE_DIST="Dist"
    P_RESIZE_REDISTRIBUTION_METHOD="Redistribution_Method"
    P_RESIZE_REDISTRIBUTION_STRATEGY="Redistribution_Strategy"
    P_RESIZE_SPAWN_METHOD="Spawn_Method"
    P_RESIZE_SPAWN_STRATEGY="Spawn_Strategy"

    @classmethod
    def has_key(cls, name):
      return any(x.value == name for x in cls)

def is_ending_of_section(line):
  if(END_SECTION_DELIMITER in line):
    return True
  return False

def is_a_general_section(line):
  if(line == GENERAL_SECTION):
    return True
  return False

def is_a_resize_section(line):
  if(RESIZE_SECTION in line and not is_ending_of_section(line)):
      return True
  return False

def is_a_stage_section(line):
  if(STAGE_SECTION in line and not is_ending_of_section(line)):
      return True
  return False

def process_line(line, data):
  key,value = line.split('=')
  if(not Config_section.has_key(key)):
    print("Unknown parameter " + key)
    return False
  
  if(',' in value):
    value = value.split(',')
    for i in range(len(value)):
      try:
        value[i] = float(value[i])
        if value[i] == int(value[i]):
            value[i] = int(value[i])
      except ValueError:
        print("Unable to convert to float - Not a fatal error")
  else:
    try:
      value = float(value)
      if value == int(value):
        value = int(value)
    except ValueError:
      print("Unable to convert to float - Not a fatal error")
    

  data[key]=value
  return True

def process_file(file_name):
  f = open(file_name, "r")
  lines = f.read().splitlines()
  section_type = Config_section.INVALID
  general_data = {}
  stages_data=[]
  resizes_data=[]
  processing=0
  for line in lines:
    if(section_type != Config_section.INVALID):
      if(is_ending_of_section(line)):
        section_type = Config_section.INVALID
      else:
        process_line(line, processing)
    elif(is_a_general_section(line)):
      section_type = Config_section.GENERAL
      processing = general_data
    elif(is_a_resize_section(line)):
      section_type = Config_section.RESIZE
      resizes_data.append({})
      processing = resizes_data[len(resizes_data)-1]
    elif(is_a_stage_section(line)):
      section_type = Config_section.STAGE
      stages_data.append({})
      processing = stages_data[len(stages_data)-1]

#  print(general_data)
#  print(stages_data)
#  print(resizes_data)
  f.close()
  return general_data,stages_data,resizes_data

def general_section_write(f, general_data):
    f.write(GENERAL_SECTION + "\n")
    keys = list(general_data.keys())
    values = list(general_data.values())
    for i in range(len(keys)):
        f.write(keys[i] + "=" + str(values[i]) + "\n")
    f.write(END_SECTION_DELIMITER + " " + GENERAL_SECTION + "\n")

def stage_section_write(f, stage_data, section_index):
    f.write(STAGE_SECTION + str(section_index) + "]\n")
    keys = list(stage_data.keys())
    values = list(stage_data.values())
    for i in range(len(keys)):
        f.write(keys[i] + "=" + str(values[i]) + "\n")
    f.write(END_SECTION_DELIMITER + " " + STAGE_SECTION + str(section_index) + "]\n")

def resize_section_write(f, resize_data, section_index):
    f.write(RESIZE_SECTION + str(section_index) + "]\n")
    keys = list(resize_data.keys())
    values = list(resize_data.values())
    for i in range(len(keys)):
        f.write(keys[i] + "=" + str(values[i]) + "\n")
    f.write(END_SECTION_DELIMITER + " " + RESIZE_SECTION + str(section_index) + "]\n")


def write_output_file(datasets, common_output_name, output_index):
    file_name = common_output_name + str(output_index) + ".ini"
    total_stages=int(datasets[0][Config_section.P_TOTAL_STAGES.value])
    total_resizes=int(datasets[0][Config_section.P_TOTAL_RESIZES.value])+1

    f = open(file_name, "w")
    general_section_write(f, datasets[0])

    for i in range(total_stages):
        stage_section_write(f, datasets[i+1], i)
    for i in range(total_resizes):
        resize_section_write(f, datasets[i+1+total_stages], i)
    f.close()
    

def check_sections_assumptions(datasets):
    total_resizes=int(datasets[0][Config_section.P_TOTAL_RESIZES.value])+1
    total_stages=int(datasets[0][Config_section.P_TOTAL_STAGES.value])

    adr = datasets[0][Config_section.P_ADR.value]
    for i in range(total_resizes):
        #Not valid if trying to use thread strategy and adr(Async data) is 0
        if adr==0 and (datasets[total_stages+1+i][Config_section.P_RESIZE_SPAWN_STRATEGY.value] == 2 or datasets[total_stages+1+i][Config_section.P_RESIZE_REDISTRIBUTION_STRATEGY.value] == 2):
            return False
        #Not valid if the strategies are different
        if datasets[total_stages+1+i][Config_section.P_RESIZE_SPAWN_STRATEGY.value] != datasets[total_stages+1+i][Config_section.P_RESIZE_REDISTRIBUTION_STRATEGY.value]:
            return False
        #Not valid if resize is to the same amount of processes
        if i>0:
            if datasets[total_stages+1+i][Config_section.P_RESIZE_PROCS.value] == datasets[total_stages+i][Config_section.P_RESIZE_PROCS.value]:
                return False
    return True

def correct_adr(general_dataset):
    sdr = general_dataset[Config_section.P_SDR.value]
    adr_percentage = general_dataset[Config_section.P_ADR.value]
    if (adr_percentage != 0):
        general_dataset[Config_section.P_ADR.value] = sdr * (adr_percentage/100)
        general_dataset[Config_section.P_SDR.value] = sdr * ((100-adr_percentage)/100)


def create_output_files(common_output_name, general_data, resize_data, stage_data):

    def read_parameter(level_index):
        dictionary = write_datasets[ds_indexes[level_index]]
        key = keys[level_index]
        index = indexes[level_index]
        max_index = mindexes[level_index]
        values = lists[level_index]
        finished=False

        if(index == max_index):
            index = 0
            if(level_index+1 == len(lists)):
                finished = True
            else:
                finished = read_parameter(level_index+1)

        dictionary[key] = values[index]
        if(key == Config_section.P_RESIZE_PROCS.value):
            original_dictionary = datasets[ds_indexes[level_index]]
            dictionary[Config_section.P_RESIZE_FACTORS.value] = original_dictionary[Config_section.P_RESIZE_FACTORS.value][index]
        indexes[level_index] = index + 1
        return finished


    datasets=[general_data]
    write_datasets=[general_data.copy()]
    for dataset in resize_data:
        datasets.append(dataset)
        write_datasets.append(dataset.copy())
    for dataset in stage_data:
        datasets.append(dataset)
        write_datasets.append(dataset.copy())

    directory = "/Desglosed-" + str(date.today())
    path = os.getcwd() + directory
    os.mkdir(path, mode=0o775)
    os.chdir(path)

    lists=[]
    keys=[]
    indexes=[]
    mindexes=[]
    ds_indexes=[]
    for i in range(len(datasets)):
        values_aux = list(datasets[i].values())
        keys_aux = list(datasets[i].keys())
        for j in range(len(values_aux)):
            if type(values_aux[j]) == list and keys_aux[j] != Config_section.P_RESIZE_FACTORS.value:
                keys.append(keys_aux[j])
                lists.append(values_aux[j])
                ds_indexes.append(i)
                indexes.append(0)
                mindexes.append(len(values_aux[j]))

    for i in range(len(lists)):
        read_parameter(i)
    output_index=0
    while True:
        if(check_sections_assumptions(write_datasets)):
            correct_adr(write_datasets[0])
            write_output_file(write_datasets, common_output_name, output_index)
#            for i in range(len(write_datasets)):
#                print(write_datasets[i])
#            print("\n\n\n------------------------------------------" + str(output_index))
            output_index+=1
        finished = read_parameter(0)
        if finished:
            break
    

if(len(sys.argv) < 3):
    print("Not enough arguments given.\nExpected usage: python3 read_multiple.py file.ini output_name")
name = sys.argv[1]
common_output_name = sys.argv[2]

general_data, resize_data, stage_data = process_file(name)
create_output_files(common_output_name, general_data, resize_data, stage_data)

exit(1)
