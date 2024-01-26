#ifndef MALLEABILITY_GENERIC_SPAWN_H
#define MALLEABILITY_GENERIC_SPAWN_H

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

int init_spawn(char *argv, int myId, int initial_qty, int target_qty, int root, MPI_Comm comm, MPI_Comm *child);
int check_spawn_state(MPI_Comm *child, MPI_Comm comm, int wait_completed);
void malleability_connect_children(int myId, int numP, int root, MPI_Comm comm, int *numP_parents, int *root_parents, MPI_Comm *parents);


void unset_spawn_postpone_flag(int outside_state);

#endif
