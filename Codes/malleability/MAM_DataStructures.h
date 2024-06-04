#ifndef MAM_DATA_STRUCTURES_H
#define MAM_DATA_STRUCTURES_H

/*
 * Shows available data structures for inner ussage.
 */
#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include <pthread.h>
#include "MAM_Constants.h"

#define DEBUG_FUNC(debug_string, rank, numP) printf("MaM [P%d/%d]: %s -- %s:%s:%d\n", rank, numP, debug_string, __FILE__, __func__, __LINE__)

/* --- MAM REAL STATES --- */
enum mam_inner_states{MAM_I_UNRESERVED, MAM_I_NOT_STARTED, MAM_I_RMS_COMPLETED, MAM_I_SPAWN_PENDING, MAM_I_SPAWN_SINGLE_PENDING, 
	MAM_I_SPAWN_SINGLE_COMPLETED, MAM_I_SPAWN_ADAPT_POSTPONE, MAM_I_SPAWN_COMPLETED, MAM_I_DIST_PENDING, MAM_I_DIST_COMPLETED, 
	MAM_I_SPAWN_ADAPT_PENDING, MAM_I_USER_START, MAM_I_USER_PENDING, MAM_I_USER_COMPLETED, MAM_I_SPAWN_ADAPTED, MAM_I_COMPLETED};

/* --- TIME CAPTURE STRUCTURE --- */
typedef struct {
  // Spawn, Sync and Async time
  double spawn_start, spawn_time;
  double sync_start,  sync_end;
  double async_start, async_end;
  double user_start, user_end;
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
  int myId, numP, numC, zombie;
  int root, root_collectives;
  int num_parents, root_parents;
  pthread_t async_thread;
  MPI_Comm comm, thread_comm, original_comm;
  MPI_Comm intercomm, tmp_comm;
  MPI_Comm *user_comm;
  MPI_Datatype struct_type;

  // Specific vars for Wait_targets strat
  int wait_targets_posted;
  MPI_Request wait_targets;
  
  char *name_exec, *nodelist;
  int num_cpus, num_nodes, nodelist_len;
  int internode_group;
} malleability_t;

/* --- VARIABLES --- */
extern malleability_config_t *mall_conf;
extern malleability_t *mall;
extern int state;

/* --- FUNCTIONS --- */
void MAM_Def_main_datatype();
void MAM_Free_main_datatype();
void MAM_Comm_main_structures(MPI_Comm comm, int rootBcast);

void MAM_print_comms_state();
void MAM_comms_update(MPI_Comm comm);

#endif
