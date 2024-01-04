#ifndef MALLEABILITY_DATA_STRUCTURES_H
#define MALLEABILITY_DATA_STRUCTURES_H

/*
 * Shows available data structures for inner ussage.
 */
#include <mpi.h>

//FIXME Remove both includes
#include "../Main/configuration.h"
#include "../Main/Main_datatypes.h"

/* --- PHYSICAL DIST STRUCTURE --- */
struct physical_dist {
  int num_cpus, num_nodes;
  char *nodelist;
  int target_qty, already_created;
  int dist_type, info_type;
};

/* --- SPAWN STRUCTURE --- */
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
} Spawn_data;

/* --- TIME CAPTURE STRUCTURE --- */
typedef struct {
  // Spawn, Sync and Async time
  double spawn_start, spawn_time;
  double sync_start,  sync_end;
  double async_start, async_end;
  double malleability_start, malleability_end;

  MPI_Datatype times_type;
} malleability_times_t;

/* --- GLOBAL STRUCTURES --- */
typedef struct {
  int spawn_method;
  int spawn_dist;
  int spawn_strategies;
  int red_method;
  int red_strategies;

  int grp;
  malleability_times_t *times;
  configuration *config_file;
} malleability_config_t;

typedef struct { //FIXME numC_spawned no se esta usando
  int myId, numP, numC, numC_spawned, root, root_parents;
  pthread_t async_thread;
  MPI_Comm comm, thread_comm;
  MPI_Comm intercomm;
  MPI_Comm user_comm;
  int dup_user_comm;
  
  char *name_exec, *nodelist;
  int num_cpus, num_nodes, nodelist_len;
} malleability_t;

/* --- VARIABLES --- */
malleability_config_t *mall_conf;
malleability_t *mall;

#endif
