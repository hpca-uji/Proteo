#ifndef MAM_SPAWN_DATASTRUCTURE_H
#define MAM_SPAWN_DATASTRUCTURE_H

#include <mpi.h>

/* --- SPAWN STRUCTURE --- */

typedef struct {
  int spawn_qty;
  char *cmd;
  MPI_Info mapping;
} Spawn_set;

typedef struct {
  int spawn_qty, initial_qty, target_qty;
  int already_created;
  int total_spawns;
  int spawn_is_single, spawn_is_async, spawn_is_intercomm, spawn_is_multiple;
//  MPI_Info mapping;
  int mapping_fill_method;

  MPI_Comm comm, returned_comm; // ONLY SET FOR SOURCE PROCESSES
  Spawn_set *sets;
} Spawn_data;

#endif
