#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <string.h>
#include <slurm/slurm.h>
#include "malleabilityStates.h"

//#define COMM_UNRESERVED -2
//#define COMM_IN_PROGRESS -1
//#define COMM_FINISHED 0

//#define COMM_PHY_NODES 1
//#define COMM_PHY_CPU 2

//#define COMM_SPAWN_SERIAL 0
//#define COMM_SPAWN_PTHREAD 1

int init_slurm_comm(char *argv, int myId, int numP, int root, int type_dist, int type_creation, MPI_Comm comm, MPI_Comm *child);
int check_slurm_comm(int myId, int root, int numP, MPI_Comm *child);
