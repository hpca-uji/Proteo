#ifndef MAM_SPAWN_UTILS_H
#define MAM_SPAWN_UTILS_H
#include <mpi.h>
#include "Spawn_DataStructure.h"

void mam_spawn(Spawn_set spawn_set, MPI_Comm comm, MPI_Comm *child);

char* get_spawn_cmd();

#endif
