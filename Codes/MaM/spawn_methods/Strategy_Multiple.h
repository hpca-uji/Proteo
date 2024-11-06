#ifndef MAM_SPAWN_MULTIPLE_H
#define MAM_SPAWN_MULTIPLE_H

#include <mpi.h>
#include "Spawn_DataStructure.h"

void multiple_strat_parents(Spawn_data spawn_data, Spawn_ports *spawn_port, MPI_Comm comm, MPI_Comm *intercomms, MPI_Comm *child);
void multiple_strat_children(MPI_Comm *parents, Spawn_ports *spawn_port);

#endif
