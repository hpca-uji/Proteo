#ifndef MAM_SPAWN_MERGE_H
#define MAM_SPAWN_MERGE_H

#include <mpi.h>
#include "Spawn_DataStructure.h"

int merge(Spawn_data spawn_data, MPI_Comm *child, int data_state);
int intracomm_strategy(int is_children_group, MPI_Comm *child);

#endif
