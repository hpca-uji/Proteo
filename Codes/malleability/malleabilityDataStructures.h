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

typedef struct { 
  int myId, numP, numC, root, zombie;
  int num_parents, root_parents;
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
