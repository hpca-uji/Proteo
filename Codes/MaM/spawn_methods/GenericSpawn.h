#ifndef MAM_GENERIC_SPAWN_H
#define MAM_GENERIC_SPAWN_H

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "Spawn_DataStructure.h"

int init_spawn(MPI_Comm comm, MPI_Comm *child);
int check_spawn_state(MPI_Comm *child, MPI_Comm comm, int wait_completed);
void malleability_connect_children(MPI_Comm *parents);


void unset_spawn_postpone_flag(int outside_state);

#endif
