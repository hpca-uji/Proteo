# Proteo - Paper Dynamic Spawning

## Overview
This branch contains an improved codebase used for the paper in section "Paper Information" in the system Nasp and the results obtained for that paper. 

## Paper Information
- **Title:** Dynamic spawning of MPI processes applied to malleability
- **Authors:** Iker Martín-Álvarez, José I. Aliaga, Maribel Castillo, Sergio Iserte, Rafael Mayo
- **Journal:** The International Journal of High Performance Computing Applications
- **Submission Date:** 03/05/2023

## Branch Structure
This branch is divided into the following 4 directories:
- **Analysis**: Contains the scripts and notebook to perform analysis of Proteo executions.
- **Codes**: Contains all the codes used to compile Proteo.
- **Exec**: Contains the scripts to execute Proteo in different ways and check if the runs have completed successfully.
- **Results**: Contains the preprocessed data and processed data used for the paper.

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
    $ make
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
