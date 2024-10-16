#ifndef MAM_SPAWN_PARALLEL_H
#define MAM_SPAWN_PARALLEL_H

#include <mpi.h>
#include "Spawn_DataStructure.h"

void parallel_strat_parents(Spawn_data spawn_data, Spawn_ports *spawn_port, MPI_Comm *child);
void parallel_strat_children(Spawn_data spawn_data, Spawn_ports *spawn_port, MPI_Comm *parents);

#endif
