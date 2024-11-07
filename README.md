# Proteo - Dev branch

## Overview
This branch contains an adaptation of the MaM library so it can be used by [DMR](https://gitlab.bsc.es/siserte/dmr/-/tree/main?ref_type=heads) 

## Branch Structure
This branch is divided into the following 4 directories:
- **Analysis**: Contains the scripts and notebook to perform analysis of Proteo executions.
- **Codes**: Contains all the codes used to compile Proteo.
- **Exec**: Contains the scripts to execute Proteo in different ways and check if the runs have completed successfully.
- **Results**: Default directory to perform executions.

Yet, this version of Proteo is not usable by itself, and is to simplify the merging to use it with DMR.

## Installation

### Prerequisites
Before installing, ensure you have the following prerequisites:
- MPI (MPICH) installed on your system. This code has been tested with MPICH versions 3.4.1 and 4.0.3 with the OFI netmod.
- Slurm is installed on your system. This code has been tested with slurm-wlm 19.05.5.


### Steps
1. Clone the repository to your local machine:

    ```bash
    $ git clone http://lorca.act.uji.es/gitlab/martini/malleability_benchmark.git
    $ cd malleability_benchmark
    $ git checkout DMR_Adaptation
    ```

2. Compile the code using the `make` command:

    ```bash
    $ cd Codes/
    $ make
    ```

    This command compiles the code using the MPI (MPICH) library.

### Clean Up
To clean the installation and remove compiled binaries, use:

```bash
$ make clean
```
