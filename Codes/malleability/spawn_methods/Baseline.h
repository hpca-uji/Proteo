#ifndef MALLEABILITY_SPAWN_BASELINE_H
#define MALLEABILITY_SPAWN_BASELINE_H

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <string.h>
#include "../malleabilityDataStructures.h"
#include "Spawn_DataStructure.h"

int baseline(Spawn_data spawn_data, MPI_Comm *child);
#endif
