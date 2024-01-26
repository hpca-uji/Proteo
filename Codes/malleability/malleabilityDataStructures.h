#ifndef MALLEABILITY_DATA_STRUCTURES_H
#define MALLEABILITY_DATA_STRUCTURES_H

/*
 * Shows available data structures for inner ussage.
 */
#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include <pthread.h>
#include "malleabilityStates.h"

#define DEBUG_FUNC(debug_string, rank, numP) printf("MaM [P%d/%d]: %s -- %s:%s:%d\n", rank, numP, debug_string, __FILE__, __func__, __LINE__)

/* --- PHYSICAL DIST STRUCTURE --- */
struct physical_dist {
  int target_qty, already_created;
  int info_type;
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
  unsigned int spawn_method;
  unsigned int spawn_dist;
  unsigned int spawn_strategies;
  unsigned int red_method;
  unsigned int red_strategies;

  malleability_times_t *times;
} malleability_config_t;

typedef struct { //FIXME numC_spawned no se esta usando
  int myId, numP, numC, root, root_parents, zombie;
  int is_intercomm;
  pthread_t async_thread;
  MPI_Comm comm, thread_comm;
  MPI_Comm intercomm, tmp_comm;
  MPI_Comm *user_comm;
  MPI_Datatype struct_type;
  
  char *name_exec, *nodelist;
  int num_cpus, num_nodes, nodelist_len;
} malleability_t;

/* --- VARIABLES --- */
malleability_config_t *mall_conf;
malleability_t *mall;
extern int state;

/* --- FUNCTIONS --- */
void MAM_Def_main_datatype();
void MAM_Free_main_datatype();
void MAM_Comm_main_structures(int rootBcast);


#endif
