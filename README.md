# Journal of Supercomputing Submission Branch

## Overview
This branch contains the codebase used for Proteo experiments and results presented for the paper in section "Paper Information" in the system Nasp. The code represents the state of the project at the time of submission and is tagged accordingly.

## Paper Information
- **Title:** Proteo: A Framework for the Generation and Evaluation of Malleable MPI Applications
- **Authors:** Iker Martín-Álvarez, José I. Aliaga, Maribel Castillo, Sergio Iserte
- **Journal:** Journal of Supercomputing
- **Submission Date:** 30/11/2023

## Branch Structure
This branch is divided into the following 4 directories:
- **Analysis**: Contains the scripts and notebook to perform analysis of Proteo executions.
- **Codes**: Contains all the codes used to compile Proteo.
- **Exec**: Contains the scripts to execute Proteo in different ways and check if the runs have completed successfully.
- **Results**: Contains the configuration files used to emulate the malleable emulation of the CG.

## Installation

### Prerequisites
Before installing, ensure you have the following prerequisites:
- MPI (MPICH) installed on your system. This code has been tested with MPICH versions 3.4.1 and 4.0.3 with the OFI netmod.
- Slurm is installed on your system. This code has been tested with slurm-wlm 19.05.5.
The following requisites are optional and only needed to process and analyse the data:
- Python 3(Optional). Only if you want to perform the post-mortem processing or analyse the data.
- Numpy 1.24.3(Optional). Only if you want to perform the post-mortem processing or analyse the data.
- Pandas 1.5.3(Optional). Only if you want to perform the post-mortem processing or analyse the data.
- Seaborn 0.12.2(Optional). Only if you want to analyse the data.
- Matplotlib 3.7.1(Optional). Only if you want to analyse the data.
- Scipy 1.10.1(Optional). Only if you want to analyse the data.
- scikit-posthocs 0.7.0(Optional). Only if you want to analyse the data.


### Steps
1. Clone the repository to your local machine:

    ```bash
    $ git clone http://lorca.act.uji.es/gitlab/martini/malleability_benchmark.git
    $ cd malleability_benchmark
    $ git checkout JournalSupercomputing23/24
    ```

2. Compile the code using the `make` command:

    ```bash
    $ cd Codes/
    $ make install_slurm
    ```

    This command compiles the code using the MPI (MPICH) library.

3. Test the installation:
    ```bash
    $ cd ../Results
    $ bash ../Exec/singleRun.sh test.ini
    ```
    This test launches an Slurm Job with a basic configuration file that performs a reconfiguration from 10 to 2 processes.
    As soon as it ends, 4 files will appear, one is the slurm output, and the other 3 are Proteo's output. 
    Example of a successful run with expected output:

    ```bash
    $ ls
    R0_G0NP10ID0.out  R0_G1NP2ID0.out  R0_Global.out  slurm-X.out
    $ bash ../Exec/CheckRun.sh test 1 1 4 2 2 100
    Number of G(2) and L(2) files match
    SUCCESS
    ```

    The slurm-X.out is the output produced by the Job, while the files beggining with an "R" are the output of Proteo, and their description can be found in the manual from this branch. 

    Lastly, the script Checkrun.sh indicates wether the execution has been performed correctly or not. The value should be SUCCESS or REPEATING, in either case Proteo has been compiled correctly. If the value is FAILURE, a major error appeared and it is recommended to contact the code mantainer.

### Clean Up
To clean the installation and remove compiled binaries, use:

```bash
$ make clean
```

## Reproducing Experiments
All the needed files to emulate the CG in Nasp are already in this branch. Keep in mind these only work properly in the system Nasp, as they have been modelled for that system.
To reproduce the experiments performed with Proteo the following steps have to be performed:

1. From the main directory of this branch execute:
    ```bash
    $ cd Results/DataRedist/Synch
    $ bash ../../../Exec/runAll.sh 5 600 > runAll.txt
    $ cd ../Asynch
    $ bash ../../../Exec/runAll.sh 5 600 > runAll.txt
    ```

    The script runAll.sh will create a job for each configuration file in the directory. Each configuration file will be run 5 times, and each run will have a Slurm limited time of 600s. The execution of both scripts create 500 Slurm jobs.

2. After all the jobs have finished, some error checking must be performed:
    ```bash
    $ cd Results/DataRedist/Synch
    $ bash ../../../Exec/CheckRun.sh config 168 5 4 2 2 600 >> Checkrun.txt
    $ cat Checkrun.txt | tail -1
    $ cd ../Asynch
    $ bash ../../../Exec/CheckRun.sh config 336 5 4 2 2 600 >> Checkrun.txt
    $ cat Checkrun.txt | tail -1
    ```

    The Checkrun.txt last line indicates the runs state for each directory. The values are: 
- SUCCESS: the directory runs have been completed. 
- FAILURE: a major error appeared, it is recommended to contact the code mantainer. 
- REPEATING: some configuration files had an error related to monitoring times and are being repeated. The Checkrun.sh script must be executed again for that directory when the new jobs finish.

When both Checkrun.txt return a SUCCESS state, the experiments have been completed and the raw data can be used. It is recommended to process it before analysing the results.

3. (Optional) When the experiments end, you can process the data. To perform this task the optional installation requisites must be meet. To process the data:
    ```bash
    $ cd Analysis/
    $ python3 MallTimes.py R ../Results/DataRedist/Synch/ dataS
    $ python3 MallTimes.py R ../Results/DataRedist/Asynch/ dataA
    $ python3 joinDf.py dataSG.pkl dataAG.pkl dataG
    $ rm dataSG.pkl dataAG.pkl
    $ python3 CreateResizeDataframe.py dataG.pkl dataM
    $ python3 CreateIterDataframe.py dataG.pkl dataL
    ```
    After these commands, you will have multiple files called dataG.pkl, dataM.pkl and dataL*.pkl. These files can be opened in Pandas as dataframes to analyse the data.

<!-- Terminar con paso 4 -->
