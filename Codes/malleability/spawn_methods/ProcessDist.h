#ifndef MALLEABILITY_SPAWN_PROCESS_DIST_H
#define MALLEABILITY_SPAWN_PROCESS_DIST_H

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <string.h>
#include "../malleabilityStates.h"
#include "../malleabilityDataStructures.h"

int physical_struct_create(int target_qty, int already_created, int info_type, struct physical_dist *dist);
void processes_dist(struct physical_dist dist, MPI_Info *info_spawn);

#endif
