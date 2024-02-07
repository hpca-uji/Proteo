import sys
import glob
import os
from datetime import date
from enum import Enum

GENERAL_SECTION = "[general]"
RESIZE_SECTION = "[resize"
STAGE_SECTION = "[stage"
END_SECTION_DELIMITER = ";end"
DIFFERENT_VALUE_DELIMITER=':'
LIST_VALUE_DELIMITER=','

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
    P_CAPTURE_METHOD="Capture_Method"

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

def convert_to_number(number):
  res = None
  try:
    res = float(number)
    if res == int(number):
      res = int(number)
  except ValueError:
    if isinstance(number, str):
      res = number
    else:
      print("Unable to convert to number - Not a fatal error")
  return res

def process_line(line, data):
  key,value = line.split('=')
  if(not Config_section.has_key(key)):
    print("Unknown parameter " + key)
    return False
  
  value = value.split(DIFFERENT_VALUE_DELIMITER) # Some keys have values that will be swapped between files
  for i in range(len(value)):
    value[i] = value[i].split(LIST_VALUE_DELIMITER) # Final config files could have multiple values for the same key
    for j in range(len(value[i])):
      value[i][j] = convert_to_number(value[i][j])
    if len(value[i]) > 1:
      value[i] = tuple(value[i])
    elif len(value[i]) == 1:
      value[i] = value[i][j]
  if len(value) == 1:
      value = value[0]

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

def key_line_write(f, keys, values):
  for i in range(len(keys)):
    f.write(keys[i] + "=")
    if type(values[i]) == tuple:
      f.write(str(values[0]))
      for j in range(len(1, values[i])):
        f.write("," + str(values[i]) )
    else:
      f.write(str(values[i]))
    f.write("\n")


def general_section_write(f, general_data):
    f.write(GENERAL_SECTION + "\n")
    keys = list(general_data.keys())
    values = list(general_data.values())

    key_line_write(f, keys, values)
    f.write(END_SECTION_DELIMITER + " " + GENERAL_SECTION + "\n")

def stage_section_write(f, stage_data, section_index):
    f.write(STAGE_SECTION + str(section_index) + "]\n")
    keys = list(stage_data.keys())
    values = list(stage_data.values())

    key_line_write(f, keys, values)
    f.write(END_SECTION_DELIMITER + " " + STAGE_SECTION + str(section_index) + "]\n")

def resize_section_write(f, resize_data, section_index):
    f.write(RESIZE_SECTION + str(section_index) + "]\n")
    keys = list(resize_data.keys())
    values = list(resize_data.values())

    key_line_write(f, keys, values)
    f.write(END_SECTION_DELIMITER + " " + RESIZE_SECTION + str(section_index) + "]\n")


def write_output_file(datasets, common_output_name, output_index):
    file_name = common_output_name + str(output_index) + ".ini"
    total_stages=int(datasets[0][Config_section.P_TOTAL_STAGES.value])
    total_groups=int(datasets[0][Config_section.P_TOTAL_RESIZES.value])+1

    f = open(file_name, "w")
    general_section_write(f, datasets[0])

    for i in range(total_stages):
        stage_section_write(f, datasets[i+1], i)
    for i in range(total_groups):
        resize_section_write(f, datasets[i+1+total_stages], i)
    f.close()
    

def check_sections_assumptions(datasets):
    total_groups=int(datasets[0][Config_section.P_TOTAL_RESIZES.value])+1
    total_stages=int(datasets[0][Config_section.P_TOTAL_STAGES.value])

    adr = datasets[0][Config_section.P_ADR.value]
    for i in range(total_groups):
        #Not valid if resize is to the same amount of processes
        if i>0:
            if datasets[total_stages+1+i][Config_section.P_RESIZE_PROCS.value] == datasets[total_stages+i][Config_section.P_RESIZE_PROCS.value]:
                return False
    return True

def correct_adr(sdr, adr_percentage, w_general_dataset):
    #TODO Tener en cuenta que tanto sdr como adr pueden tener diferentes valores
    if (adr_percentage != 0):
        w_general_dataset[Config_section.P_ADR.value] = sdr * (adr_percentage/100)
    w_general_dataset[Config_section.P_SDR.value] = sdr * ((100.0-adr_percentage)/100)


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
        elif(key == Config_section.P_SDR.value or key == Config_section.P_ADR.value):
            original_dictionary = datasets[ds_indexes[level_index]]
            sdr = original_dictionary[Config_section.P_SDR.value]
            adr_percentage = original_dictionary[Config_section.P_ADR.value][index]
            correct_adr(sdr, adr_percentage, dictionary)

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

    lists=[] # Stores lists of those variables with multiple values
    keys=[] # Stores keys of those variables with multiple values
    indexes=[] # Stores actual index for each variable with multiple values. Always starts at 0.
    mindexes=[] # Stores len of lists of each variable with multiple values
    ds_indexes=[] # Stores the index of the dataset where the variable is stored
    #For each variable with a list of elements
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

    directory = "/Desglosed-" + str(date.today())
    path = os.getcwd() + directory
    os.mkdir(path, mode=0o775)
    os.chdir(path)

    #Get the first set of values
    for i in range(len(lists)):
        read_parameter(i)

    #FIXME Deberia hacerse en otra parte
    if (type(datasets[0][Config_section.P_SDR.value]) != list or type(datasets[0][Config_section.P_ADR.value]) != list):
        sdr = datasets[0][Config_section.P_SDR.value]
        adr_percentage = datasets[0][Config_section.P_ADR.value]
        correct_adr(sdr, adr_percentage, write_datasets[0])

    output_index=0
    adr_corrected=False
    finished = False
    while not finished:
        if(check_sections_assumptions(write_datasets)):
            write_output_file(write_datasets, common_output_name, output_index)
#            for i in range(len(write_datasets)):
#                print(write_datasets[i])
#            print("\n\n\n------------------------------------------" + str(output_index) + " ADR=" + str(adr_corrected))
            output_index+=1
        finished = read_parameter(0)
#=====================================================     

if(len(sys.argv) < 3):
    print("Not enough arguments given.\nExpected usage: python3 read_multiple.py file.ini output_name")
name = sys.argv[1]
common_output_name = sys.argv[2]

general_data, resize_data, stage_data = process_file(name)
create_output_files(common_output_name, general_data, resize_data, stage_data)

exit(1)
