import sys
import glob

def general(f, resizes, matrix_tam, comm_tam, sdr, adr, aib, cst, css, time):
    f.write("[general]\n")
    f.write("resizes=" + resizes +"\n")
    f.write("matrix_tam=" + matrix_tam +"\n")
    f.write("comm_tam=" + comm_tam +"\n")
    f.write("SDR=" + sdr +"\n")
    f.write("ADR=" + adr +"\n")
    f.write("AIB=" + aib +"\n")
    f.write("CST=" + cst +"\n")
    f.write("CSS=" + css +"\n")
    f.write("time=" + time +"\n")
    f.write("; end [general]\n")

def resize_section(f, resize, iters, procs, factor, physical_dist):
    f.write("[resize" + resize + "]\n")
    f.write("iters=" + iters +"\n")
    f.write("procs=" + procs +"\n")
    f.write("factor=" + factor +"\n")
    f.write("physical_dist=" + physical_dist +"\n")
    f.write(";end [resize" + resize +"]\n")

if len(sys.argv) < 2:
  print("The config file name is missing\nUsage: python3 program nameFile args\nArgs: resizes matrix_tam SDR ADR AIB CST CSS time iters0 procs0 dist0 iters1 procs1 dist1 ...")
  exit(1)

if len(sys.argv) < 12:
  print("The are not enough arguments\nUsage: python3 program nameFile args\nArgs: resizes matrix_tam SDR ADR_perc AIB CST CSS time proc_time iters0 procs0 dist0 iters1 procs1 dist1 ...")
  exit(1)

name = sys.argv[1]
resizes = int(sys.argv[2])
matrix_tam = sys.argv[3]
comm_tam = sys.argv[4]
sdr = int(sys.argv[5])
adr_perc = float(sys.argv[6])
aib = sys.argv[7]
cst = sys.argv[8]
css = sys.argv[9]
time = sys.argv[10]
proc_time = float(sys.argv[11]) # Usado para calcular el factor de cada proceso

adr = (sdr * adr_perc) / 100
sdr = sdr - adr

adr = str(adr)
sdr = str(sdr)

factor = 0
f = open(name, "w")
general(f, str(resizes), matrix_tam, comm_tam, sdr, adr, aib, cst, css, time)

resizes = resizes + 1 # Internamente, los primeros procesos se muestran como un grupo
for resize in range(resizes):
    iters = sys.argv[12 + 3 * resize]
    procs = sys.argv[12 + 3 * resize + 1]
    physical_dist = sys.argv[12 + 3 * resize + 2]

    if proc_time != 0: # Si el argumento proc_time es 0, todos los grupos tienen un factor de 1
        factor = proc_time / float(procs)
    else:
        factor = 1

    resize_section(f, str(resize), iters, procs, str(factor), physical_dist)

f.close()
exit(1)
