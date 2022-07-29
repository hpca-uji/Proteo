#ifndef MALLEABILITY_GENERIC_SPAWN_H
#define MALLEABILITY_GENERIC_SPAWN_H

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "../malleabilityDataStructures.h"

int init_spawn(char *argv, int num_cpus, int num_nodes, char *nodelist, int myId, int initial_qty, int target_qty, int root, int type_dist, int spawn_method, int spawn_strategies, MPI_Comm comm, MPI_Comm *child);
int check_spawn_state(MPI_Comm *child, MPI_Comm comm, int data_dist_completed, double *real_time);

void malleability_connect_children(int myId, int numP, int root, int *numP_parents, int *root_parents, MPI_Comm *parents);

#endif
