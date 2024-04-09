#ifndef MALLEABILITY_SPAWN_PROCESS_DIST_H
#define MALLEABILITY_SPAWN_PROCESS_DIST_H

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <string.h>
#include "../malleabilityStates.h"
#include "../malleabilityDataStructures.h"
#include "Spawn_DataStructure.h"

void processes_dist(Spawn_data *spawn_data);

#endif
