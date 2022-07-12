#ifndef MALLEABILITY_DATA_STRUCTURES_H
#define MALLEABILITY_DATA_STRUCTURES_H

/*
 * Shows available data structures for inner ussage.
 */
#include <mpi.h>

/* --- SPAWN STRUCTURES --- */
struct physical_dist {
  int num_cpus, num_nodes;
  char *nodelist;
  int target_qty, already_created;
  int dist_type;
};

typedef struct {
  int myId, root, root_parents;
  int spawn_qty, initial_qty, target_qty;
  int already_created;
  int spawn_method, spawn_is_single, spawn_is_async;
  char *cmd; //Executable name
  MPI_Info mapping;
  MPI_Datatype dtype;
  struct physical_dist dist; // Used to create mapping var

  MPI_Comm comm, returned_comm;

  // To control the spawn state
  pthread_mutex_t spawn_mutex;
  pthread_cond_t cond_adapt_rdy;
} Spawn_data;

#endif
