#ifndef MALLEABILITY_SPAWN_MERGE_H
#define MALLEABILITY_SPAWN_MERGE_H

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "../malleabilityDataStructures.h"
#include "Spawn_DataStructure.h"

int merge(Spawn_data spawn_data, MPI_Comm *child, int data_state);
int intracomm_strategy(int is_children_group, MPI_Comm *child);

#endif
