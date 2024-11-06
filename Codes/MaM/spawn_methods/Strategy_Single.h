#ifndef MAM_SPAWN_SINGLE_H
#define MAM_SPAWN_SINGLE_H

#include <mpi.h>
#include "Spawn_DataStructure.h"

void single_strat_parents(Spawn_data spawn_data, MPI_Comm *child);
void single_strat_children(MPI_Comm *parents, Spawn_ports *spawn_port);

#endif
