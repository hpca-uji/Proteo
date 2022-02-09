#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <string.h>
#include <slurm/slurm.h>
#include "malleabilityStates.h"

int init_slurm_comm(char *argv, int myId, int numP, int numC, int root, int type_dist, int type_creation, int spawn_is_single, MPI_Comm comm, MPI_Comm *child);
int check_slurm_comm(int myId, int root, int numP, MPI_Comm *child, MPI_Comm comm, MPI_Comm comm_thread, double *end_real_time);

void malleability_establish_connection(int myId, int root, MPI_Comm *intercomm);


void proc_adapt_expand(int *numP, int numC, MPI_Comm intercomm, MPI_Comm *comm, int is_children_group);
void proc_adapt_shrink(int numC, MPI_Comm *comm, int myId);
