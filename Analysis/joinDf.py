import sys
import glob
import numpy as numpy
import pandas as pd

if len(sys.argv) < 3:
    print("The files name is missing\nUsage: python3 joinDf.py resultsName1.pkl resultsName2.pkl OutName")
    exit(1)

if len(sys.argv) >= 4:
  name = sys.argv[3]
else:
  name = "dataJOINED"

print("File name will be: " + name + ".pkl")
df1 = pd.read_pickle( sys.argv[1] )
df2 = pd.read_pickle( sys.argv[2] )
frames = [df1, df2]
df3 = pd.concat(frames)
df3 = df3.drop(columns=df3.columns[0])

df3.to_pickle(name + '.pkl')
