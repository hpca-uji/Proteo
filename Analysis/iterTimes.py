import sys
import glob
import numpy as numpy
import pandas as pd

#-----------------------------------------------
def read_file(f, dataA, dataB, itA, itB):
  compute_tam = 0
  comm_tam = 0
  sdr = 0
  adr = 0
  dist = 0
  css = 0
  cst = 0
  time = 0
  recording = False
  it_line = 0
  aux_itA = 0
  aux_itB = 0
  iters = 0
  np = 0
  np_par = 0
  ns = 0
  array = []
  columnas = ['Titer','Ttype','Top']

  #print(f)
  for line in f: 
    lineS = line.split()
    if len(lineS) > 1:

        if recording and lineS[0].split(':')[0] in columnas: #Record data
          aux_itA = 0
          lineS.pop(0)

          if it_line==0:
            for observation in lineS:
              dataA.append([None]*15)
              dataA[itA+aux_itA][0] = sdr
              dataA[itA+aux_itA][1] = adr
              dataA[itA+aux_itA][2] = np
              dataA[itA+aux_itA][3] = np_par
              dataA[itA+aux_itA][4] = ns
              dataA[itA+aux_itA][5] = dist
              dataA[itA+aux_itA][6] = compute_tam
              dataA[itA+aux_itA][7] = comm_tam
              dataA[itA+aux_itA][8] = cst
              dataA[itA+aux_itA][9] = css
              dataA[itA+aux_itA][10] = time
              dataA[itA+aux_itA][11] = iters
              dataA[itA+aux_itA][12] = float(observation)
              array.append(float(observation))
              aux_itA+=1
          elif it_line==1:
            deleted = 0
            for observation in lineS:
              dataA[itA+aux_itA][13] = float(observation)
              if float(observation) == 0:
                  array.pop(aux_itA - deleted)
                  deleted+=1
              aux_itA+=1
          else:
            for observation in lineS:
              dataA[itA+aux_itA][14] = float(observation)
              aux_itA+=1

          it_line += 1
          if(it_line % 3 == 0): # Comprobar si se ha terminado de mirar esta ejecucion
            recording = False
            it_line = 0
            itA = itA + aux_itA
            if ns != 0: # Solo obtener datos de grupos con hijos
                dataB.append([None]*14)
                dataB[itB][0] = sdr
                dataB[itB][1] = adr
                dataB[itB][2] = np
                dataB[itB][3] = np_par
                dataB[itB][4] = ns
                dataB[itB][5] = dist
                dataB[itB][6] = compute_tam
                dataB[itB][7] = comm_tam
                dataB[itB][8] = cst
                dataB[itB][9] = css
                dataB[itB][10] = time
                dataB[itB][11] = iters
                dataB[itB][12] = tuple(array)
                dataB[itB][13] = numpy.sum(array)
                itB+=1
                array = []

        if lineS[0] == "Config:":
          compute_tam = int(lineS[1].split('=')[1].split(',')[0])
          comm_tam = int(lineS[2].split('=')[1].split(',')[0])
          sdr = int(lineS[3].split('=')[1].split(',')[0])
          adr = int(lineS[4].split('=')[1].split(',')[0]) 
          css = int(lineS[6].split('=')[1].split(',')[0])
          cst = int(lineS[7].split('=')[1].split(',')[0])
          time = float(lineS[8].split('=')[1])
        elif lineS[0] == "Config":
          recording = True
          iters = int(lineS[2].split('=')[1].split(',')[0])
          dist = int(lineS[4].split('=')[1].split(',')[0])
          np = int(lineS[5].split('=')[1].split(',')[0])
          np_par = int(lineS[6].split('=')[1].split(',')[0])
          ns = int(float(lineS[7].split('=')[1]))

  return itA,itB
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
  print("Csv name will be: " + sys.argv[3] + ".csv and "+ sys.argv[3] + "_Total.csv")
  name = sys.argv[3]
else:
  name = "data"

insideDir = "Run"
lista = glob.glob("./" + BaseDir + insideDir + "*/" + sys.argv[1]+ "*ID*.o*")
print("Number of files found: "+ str(len(lista)));

itA = itB = 0
dataA = []
dataB = []  #0   #1        #2    #3       #4     #5      #6             #7          #8     #9   #10    #11       #12   #13   #14
columnsA = ["N", "%Async", "NP", "N_par", "NS", "Dist", "Compute_tam", "Comm_tam", "Cst", "Css","Time", "Iters", "Ti", "Tt", "To"] #15
columnsB = ["N", "%Async", "NP", "N_par", "NS", "Dist", "Compute_tam", "Comm_tam", "Cst", "Css","Time", "Iters", "Ti", "Sum"] #14

for elem in lista:
  f = open(elem, "r")
  itA,itB = read_file(f, dataA, dataB, itA, itB)
  f.close()

#print(data)
dfA = pd.DataFrame(dataA, columns=columnsA)
dfB = pd.DataFrame(dataB, columns=columnsB)

dfA['N'] += dfA['%Async']
dfA['%Async'] = (dfA['%Async'] / dfA['N']) * 100
dfA.to_csv(name + '.csv')

dfB['N'] += dfB['%Async']
dfB['%Async'] = (dfB['%Async'] / dfB['N']) * 100
dfB.to_csv(name + '_Total.csv')
