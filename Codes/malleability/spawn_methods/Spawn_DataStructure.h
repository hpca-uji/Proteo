
#ifndef MAM_SPAWN_DATASTRUCTURE_H
#define MAM_SPAWN_DATASTRUCTURE_H

#include <mpi.h>

/* --- SPAWN STRUCTURE --- */
typedef struct {
  int spawn_qty, initial_qty, target_qty;
  int already_created;
  int spawn_is_single, spawn_is_async, spawn_is_intercomm;
  MPI_Info mapping;
  int mapping_fill_method;

  MPI_Comm comm, returned_comm;
} Spawn_data;

#endif
