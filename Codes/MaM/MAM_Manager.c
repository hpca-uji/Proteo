/**
 * @file MAM_Manager.c
 * @brief Implementation of the MaM public API and of the reconfiguration state machine.
 *
 * This file drives a complete reconfiguration cycle. Terminology used throughout:
 *   - Sources: the ranks that exist before the reconfiguration. They are also the
 *     parents of any dynamically spawned process.
 *   - Children: the ranks created by the spawn step. Children are always targets.
 *   - Targets: the ranks that keep running after the reconfiguration. With the
 *     Baseline spawn method the targets are exactly the children; with the Merge
 *     spawn method the targets are the children plus the reused sources.
 *
 * Progress is made by repeated calls to ::MAM_Checkpoint, which dispatches on the
 * global @c state variable and delegates to one @c MAM_St_* stage handler per state.
 * Each stage handler returns non-zero when the state machine advanced far enough
 * that another dispatch can be performed immediately.
 */

#include <pthread.h>
#include <string.h>
#include "MAM.h"
#include "MAM_Constants.h"
#include "MAM_DataStructures.h"
#include "MAM_Types.h"
#include "MAM_Zombies.h"
#include "MAM_Times.h"
#include "MAM_RMS.h"
#include "MAM_Init_Configuration.h"
#include "GenericSpawn.h"
#include "Distributed_CommDist.h"

/** @brief Flag passed to send_data()/recv_data() to request blocking transfers. */
#define MAM_USE_SYNCHRONOUS 0
/** @brief Flag passed to send_data()/recv_data() to request non-blocking transfers. */
#define MAM_USE_ASYNCHRONOUS 1

void MAM_Commit(int *o_mam_state);

/*
 * Stage handlers of the reconfiguration state machine, in the order in which
 * MAM_Checkpoint() dispatches them. MAM_St_spawn_adapted() and MAM_St_red_completed()
 * are declared for symmetry with the internal states but have no definition here;
 * those states are handled by MAM_St_completed().
 */
int MAM_St_rms(int *o_mam_state);
int MAM_St_spawn_start(void);
int MAM_St_spawn_pending(int i_wait_completed);
int MAM_St_red_start(void);
int MAM_St_red_pending(int i_wait_completed);
int MAM_St_user_start(int *o_mam_state);
int MAM_St_user_pending(int *o_mam_state, int i_wait_completed, void (*i_user_function)(void *), void *i_user_args);
int MAM_St_user_completed(void);
int MAM_St_spawn_adapt_pending(int i_wait_completed);
int MAM_St_spawn_adapted(int *o_mam_state);
int MAM_St_red_completed(int *o_mam_state);
int MAM_St_completed(int *o_mam_state);


/*
 * Steps performed by the children and by the sources. Merge shrinks are completed
 * by MAM_St_spawn_adapt_pending() instead.
 */
void Children_init(void (*i_user_function)(void *), void *i_user_args);
int spawn_step(void);
int start_redistribution(void);
int check_redistribution(int i_wait_completed);
int end_redistribution(void);

/* Background redistribution carried out by an auxiliary pthread. */
int thread_creation(void);
int thread_check(int i_wait_completed);
void* thread_async_work();

/* Internal helpers. MAM_I_convert_key() has no definition in this file. */
int MAM_I_convert_key(char *i_key);
void MAM_I_create_user_struct(int i_is_children_group);

/*
 * The four data registries. MAM_Data_add() selects one of them from the pair of
 * flags (is_replicated, is_constant):
 *   - is_constant == MAM_DATA_CONSTANT selects an "_a_" (asynchronous) registry,
 *     because constant data never changes and can therefore be transferred in the
 *     background, overlapped with the rest of the reconfiguration.
 *   - is_constant == MAM_DATA_VARIABLE selects an "_s_" (synchronous) registry,
 *     because variable data must be transferred once the application has stopped
 *     modifying it, i.e. blockingly and late in the cycle.
 *   - is_replicated == MAM_DATA_REPLICATED selects a "rep_" registry, whose entries
 *     hold the same values on every rank and are propagated with a broadcast.
 *   - is_replicated == MAM_DATA_DISTRIBUTED selects a "dist_" registry, whose
 *     entries are partitioned across ranks and are propagated with send_data()/
 *     recv_data() using the configured redistribution method.
 */

/** @brief Replicated + synchronous registry (variable data, broadcast at the end). */
malleability_data_t *rep_s_data;
/** @brief Distributed + synchronous registry (variable data, redistributed at the end). */
malleability_data_t *dist_s_data;
/** @brief Replicated + asynchronous registry (constant data, broadcast in background). */
malleability_data_t *rep_a_data;
/** @brief Distributed + asynchronous registry (constant data, redistributed in background). */
malleability_data_t *dist_a_data;

/** @brief Snapshot handed to the application through ::MAM_Get_Reconf_Info. */
mam_user_reconf_t *user_reconf;

/**
 * @brief Initialise MaM, or finish joining as a dynamically spawned child group.
 *
 * Allocates the internal configuration and the four data registries, and duplicates
 * the application communicator so that MaM never interferes with the application's
 * own communication.
 *
 * If the calling group was created dynamically (it has an MPI parent), the group
 * instead connects to its parents through Children_init() and returns ready to run
 * the application.
 *
 * @param[in]     i_root          Rank acting as root among the sources.
 * @param[in,out] io_comm         Application communicator; kept as the user
 *                                communicator and refreshed on every commit.
 * @param[in]     i_name_exec     Executable name used later by the spawn step.
 * @param[in]     i_user_function Optional user callback for the user phase.
 * @param[in]     i_user_args     Opaque argument forwarded to @p i_user_function.
 * @return @c MAM_TARGETS when called by a spawned group, @c MAM_SOURCES otherwise.
 */
int MAM_Init(int i_root, MPI_Comm *io_comm, char *i_name_exec, void (*i_user_function)(void *), void *i_user_args) {
  MPI_Comm dup_comm, thread_comm, original_comm;

  mall_conf = (malleability_config_t *) malloc(sizeof(malleability_config_t));
  mall = (malleability_t *) malloc(sizeof(malleability_t));
  user_reconf = (mam_user_reconf_t *) malloc(sizeof(mam_user_reconf_t));

  MPI_Comm_rank(*io_comm, &(mall->myId));
  MPI_Comm_size(*io_comm, &(mall->numP));

  #if MAM_DEBUG
    DEBUG_FUNC("Initializing MaM", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(*io_comm);
  #endif

  rep_s_data = (malleability_data_t *) malloc(sizeof(malleability_data_t));
  dist_s_data = (malleability_data_t *) malloc(sizeof(malleability_data_t));
  rep_a_data = (malleability_data_t *) malloc(sizeof(malleability_data_t));
  dist_a_data = (malleability_data_t *) malloc(sizeof(malleability_data_t));

  MPI_Comm_dup(*io_comm, &dup_comm);
  MPI_Comm_dup(*io_comm, &thread_comm);
  MPI_Comm_dup(*io_comm, &original_comm);
  MPI_Comm_set_name(dup_comm, "MAM_MAIN");
  MPI_Comm_set_name(thread_comm, "MAM_THREAD");
  MPI_Comm_set_name(original_comm, "MAM_ORIGINAL");

  mall->root = i_root;
  mall->root_parents = i_root;
  mall->zombie = 0;
  mall->comm = dup_comm;
  mall->thread_comm = thread_comm;
  mall->original_comm = original_comm;
  mall->user_comm = io_comm; 
  mall->tmp_comm = MPI_COMM_NULL;
  mall->intercomm = MPI_COMM_NULL;

  mall->name_exec = i_name_exec;
  mall->nodelist = NULL;
  mall->max_cpus = NULL;
  mall->assigned_cpus = NULL;
  mall->spawned_cpus = NULL;
  mall->nodelist_len = 0;

  rep_s_data->entries = 0;
  rep_a_data->entries = 0;
  dist_s_data->entries = 0;
  dist_a_data->entries = 0;

  state = MAM_I_NOT_STARTED;

  MAM_Init_configuration();
  MAM_Zombies_service_init();
  init_malleability_times();
  MAM_Def_main_datatype();

  // Children obtain their data from the parents that spawned them
  MPI_Comm_get_parent(&(mall->intercomm));
  if(mall->intercomm != MPI_COMM_NULL) { 
    Children_init(i_user_function, i_user_args);
    return MAM_TARGETS;
  }

  //TODO: Check potential improvement - If check_hosts does not use slurm, internode_group could be obtained there
  MAM_check_hosts();
  mall->internode_group = MAM_Is_internode_group();
  MAM_Set_initial_configuration();

  #if MAM_USE_BARRIERS && MAM_DEBUG
    if(mall->myId == mall->root)
      printf("MaM: Using barriers to record times.\n");
  #endif

  #if MAM_DEBUG
    DEBUG_FUNC("MaM has been initialized correctly as parents", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(*io_comm);
  #endif

  return MAM_SOURCES;
}

/**
 * @brief Release every resource reserved by MaM and wake up pending zombies.
 *
 * Frees the four registries, the host/CPU bookkeeping arrays, the MaM datatypes and
 * the duplicated communicators, and shuts down the zombie service.
 *
 * @return Non-zero if the zombie service requests the caller to abort, zero otherwise.
 */
int MAM_Finalize(void) {	  
  int request_abort;
  free_malleability_data_struct(rep_s_data);
  free_malleability_data_struct(rep_a_data);
  free_malleability_data_struct(dist_s_data);
  free_malleability_data_struct(dist_a_data);

  free(rep_s_data);
  free(rep_a_data);
  free(dist_s_data);
  free(dist_a_data);
  if(mall->nodelist != NULL) free(mall->nodelist);
  if(NULL != mall->max_cpus) { free(mall->max_cpus); }
  if(NULL != mall->assigned_cpus) { free(mall->assigned_cpus); }
  if(NULL != mall->spawned_cpus) { free(mall->spawned_cpus); }

  MAM_Free_main_datatype();
  request_abort = MAM_Zombies_service_free();
  free_malleability_times();
  if(mall->comm != MPI_COMM_WORLD && mall->comm != MPI_COMM_NULL) MPI_Comm_disconnect(&(mall->comm));
  if(mall->thread_comm != MPI_COMM_WORLD && mall->thread_comm != MPI_COMM_NULL) MPI_Comm_disconnect(&(mall->thread_comm));
  if(mall->intercomm != MPI_COMM_WORLD && mall->intercomm != MPI_COMM_NULL) { MPI_Comm_disconnect(&(mall->intercomm)); } //FIXME: Error in OpenMPI + Merge
  if(mall->original_comm != MPI_COMM_WORLD && mall->original_comm != MPI_COMM_NULL) MPI_Comm_free(&(mall->original_comm));
  free(mall);
  free(mall_conf);
  free(user_reconf);

  state = MAM_I_UNRESERVED;
  return request_abort;
}

/**
 * @brief Advance the reconfiguration state machine by one dispatch.
 *
 * Checks the current malleability state and tries to move it forward. Acts as a
 * state machine: each internal state is handled by its own @c MAM_St_* function,
 * and when a handler reports that further progress is immediately possible this
 * function recurses so that several stages can be traversed in a single call.
 *
 * @param[out] o_mam_state      Receives the generic (public) malleability state.
 * @param[in]  i_wait_completed @c MAM_WAIT_COMPLETION to block until the work
 *                              currently carried out by MaM finishes, or
 *                              @c MAM_CHECK_COMPLETION to only test for progress.
 * @param[in]  i_user_function  Callback invoked during the user redistribution
 *                              phase; may be @c NULL to skip that phase.
 * @param[in]  i_user_args      Opaque argument forwarded to @p i_user_function.
 * @return The concrete internal malleability state after the dispatch.
 *
 * @todo Rewrite this description once the stage list stabilises.
 */
int MAM_Checkpoint(int *o_mam_state, int i_wait_completed, void (*i_user_function)(void *), void *i_user_args) {
  int call_checkpoint = 0;

  //TODO: This could be changed to an array with the functions to call in each case
  switch(state) {
    case MAM_I_UNRESERVED:
      *o_mam_state = MAM_UNRESERVED;
      break;
    case MAM_I_NOT_STARTED:
      call_checkpoint = MAM_St_rms(o_mam_state);
      break;
    case MAM_I_RMS_COMPLETED:
      call_checkpoint = MAM_St_spawn_start();
      break;

    case MAM_I_SPAWN_PENDING: // Check whether the spawn has finished
    case MAM_I_SPAWN_SINGLE_PENDING:
      call_checkpoint = MAM_St_spawn_pending(i_wait_completed);
      break;

    case MAM_I_SPAWN_ADAPT_POSTPONE:
    case MAM_I_SPAWN_COMPLETED:
      call_checkpoint = MAM_St_red_start();
      break;

    case MAM_I_DIST_PENDING:
      call_checkpoint = MAM_St_red_pending(i_wait_completed);
      break;

    case MAM_I_USER_START:
      call_checkpoint = MAM_St_user_start(o_mam_state);
      break;

    case MAM_I_USER_PENDING:
      call_checkpoint = MAM_St_user_pending(o_mam_state, i_wait_completed, i_user_function, i_user_args);
      break;

    case MAM_I_USER_COMPLETED:
      call_checkpoint = MAM_St_user_completed();
      break;

    case MAM_I_SPAWN_ADAPT_PENDING:
      call_checkpoint = MAM_St_spawn_adapt_pending(i_wait_completed);
      break;

    case MAM_I_SPAWN_ADAPTED:
    case MAM_I_DIST_COMPLETED:
      call_checkpoint = MAM_St_completed(o_mam_state);
      break;
  }

  if(call_checkpoint) { MAM_Checkpoint(o_mam_state, i_wait_completed, i_user_function, i_user_args); }
  if(state > MAM_I_NOT_STARTED && state < MAM_I_COMPLETED) *o_mam_state = MAM_PENDING;
  return state;
}

/**
 * @brief Signal that the user-driven data redistribution has finished.
 *
 * Called by the application from within its user callback so that the
 * reconfiguration can proceed to its following stages.
 *
 * @param[out] o_mam_state Receives @c MAM_PENDING; ignored when @c NULL.
 */
void MAM_Resume_redistribution(int *o_mam_state) {
  state = MAM_I_USER_COMPLETED;
  if(o_mam_state != NULL) *o_mam_state = MAM_PENDING;
}

/**
 * @brief Close a reconfiguration, cleaning up the structures it used.
 *
 * Used internally by MaM once every transfer is done. It records the final times,
 * updates the CPU accounting, releases the temporary communicators, terminates the
 * ranks that became zombies, rebuilds the working communicator for the surviving
 * targets and hands a fresh duplicate back to the application.
 *
 * Ranks flagged as zombies never return from this function: they finalise MaM and
 * MPI and exit the process.
 *
 * @param[out] o_mam_state Receives @c MAM_COMPLETED; ignored when @c NULL.
 */
void MAM_Commit(int *o_mam_state) {
  int request_abort;
  #if MAM_DEBUG
    if(mall->myId == mall->root){ DEBUG_FUNC("Trying to commit", mall->myId, mall->numP); } fflush(stdout);
  #endif

  // Get times before commiting
  if(mall_conf->spawn_method == MAM_SPAWN_BASELINE) {
    // This communication is only needed when the root process will become a zombie
    malleability_times_broadcast(mall->root_collectives);
    // Change assigned_cpus to spawned_cpus
    free(mall->assigned_cpus); mall->assigned_cpus = NULL;
    mall->assigned_cpus = mall->spawned_cpus;
    mall->spawned_cpus = calloc(mall->num_nodes, sizeof *mall->spawned_cpus);

    for(int i=0; i < mall->num_nodes; i++) {
      mall->spawned_cpus[i] = 0;
    }
  } else {
    for(int i=0; i < mall->num_nodes; i++) {
      mall->assigned_cpus[i] += mall->spawned_cpus[i];
      mall->spawned_cpus[i] = 0;
    }
  }

  // Free unneded communicators
  if(mall->tmp_comm != MPI_COMM_WORLD && mall->tmp_comm != MPI_COMM_NULL) MPI_Comm_disconnect(&(mall->tmp_comm));
  if(*(mall->user_comm) != MPI_COMM_WORLD && *(mall->user_comm) != MPI_COMM_NULL) MPI_Comm_disconnect(mall->user_comm);

  // Zombies Treatment
  MAM_Zombies_update();
  if(mall->zombie) {
    #if MAM_DEBUG >= 1
      DEBUG_FUNC("Is terminating as zombie", mall->myId, mall->numP); fflush(stdout);
    #endif
    request_abort = MAM_Finalize();
    if(request_abort) { MPI_Abort(MPI_COMM_WORLD, -101); }
    MPI_Finalize();
    exit(0);
  }

  // Reset/Free communicators
  if(mall_conf->spawn_method == MAM_SPAWN_MERGE) { MAM_comms_update(mall->intercomm); }
  if(mall->intercomm != MPI_COMM_NULL && mall->intercomm != MPI_COMM_WORLD) { MPI_Comm_disconnect(&(mall->intercomm)); } //FIXME: Error in OpenMPI + Merge

  MPI_Comm_rank(mall->comm, &mall->myId);
  MPI_Comm_size(mall->comm, &mall->numP);
  mall->root = mall_conf->spawn_method == MAM_SPAWN_BASELINE ? mall->root : mall->root_parents;
  mall->root_parents = mall->root;
  state = MAM_I_NOT_STARTED;
  if(o_mam_state != NULL) *o_mam_state = MAM_COMPLETED;

  // Set new communicator
  MPI_Comm_dup(mall->comm, mall->user_comm);
  #if MAM_DEBUG
    if(mall->myId == mall->root) { DEBUG_FUNC("Reconfiguration has been commited", mall->myId, mall->numP); fflush(stdout); }
  #endif

  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->comm);
  #endif
  mall_conf->times->malleability_end = MPI_Wtime();
}

/**
 * @brief Register a data array in one of the four registries.
 *
 * The target registry is selected from the two flags: @p i_is_constant chooses
 * between the asynchronous (constant) and the synchronous (variable) registries,
 * while @p i_is_replicated chooses between the replicated and the distributed ones.
 * For constant distributed data the number of communication requests reserved per
 * entry depends on the configured redistribution method: one request for the
 * Baseline collective method, and one per target for the point-to-point and RMA
 * methods.
 *
 * @param[in]  i_data          Pointer to the data to be added.
 * @param[out] o_index         Receives the index of the newly added entry;
 *                             ignored when @c NULL.
 * @param[in]  i_total_qty     Amount of elements in @p i_data.
 * @param[in]  i_type          MPI datatype of the elements.
 * @param[in]  i_is_replicated @c MAM_DATA_REPLICATED or @c MAM_DATA_DISTRIBUTED.
 * @param[in]  i_is_constant   @c MAM_DATA_CONSTANT (asynchronous transfer) or
 *                             @c MAM_DATA_VARIABLE (synchronous transfer).
 */
void MAM_Data_add(void *i_data, size_t *o_index, size_t i_total_qty, MPI_Datatype i_type, int i_is_replicated, int i_is_constant) {
  size_t total_reqs = 0, returned_index;

  if(i_is_constant) { //Async
    if(i_is_replicated) {
      total_reqs = 1;
      add_data(i_data, i_total_qty, i_type, total_reqs, rep_a_data);
      returned_index = rep_a_data->entries-1;
    } else {
      if(mall_conf->red_method  == MAM_RED_BASELINE) {
        total_reqs = 1;
      } else if(mall_conf->red_method  == MAM_RED_POINT || mall_conf->red_method  == MAM_RED_RMA_LOCK || mall_conf->red_method  == MAM_RED_RMA_LOCKALL) {
        total_reqs = mall->numC;
      } 
      
      add_data(i_data, i_total_qty, i_type, total_reqs, dist_a_data);
      returned_index = dist_a_data->entries-1;
    }
  } else { //Sync
    if(i_is_replicated) {
      add_data(i_data, i_total_qty, i_type, total_reqs, rep_s_data);
      returned_index = rep_s_data->entries-1;
    } else {
      add_data(i_data, i_total_qty, i_type, total_reqs, dist_s_data);
      returned_index = dist_s_data->entries-1;
    }
  }

  if(o_index != NULL) *o_index = returned_index;
}

/**
 * @brief Modify an already registered entry of one of the four registries.
 *
 * The registry is selected exactly as in ::MAM_Data_add, and the request count for
 * constant distributed data is recomputed from the configured redistribution method.
 *
 * @param[in] i_data          Pointer to the new data.
 * @param[in] i_index         Index of the entry to be modified.
 * @param[in] i_total_qty     Amount of elements in @p i_data.
 * @param[in] i_type          MPI datatype of the elements.
 * @param[in] i_is_replicated @c MAM_DATA_REPLICATED or @c MAM_DATA_DISTRIBUTED.
 * @param[in] i_is_constant   @c MAM_DATA_CONSTANT (asynchronous transfer) or
 *                            @c MAM_DATA_VARIABLE (synchronous transfer).
 */
void MAM_Data_modify(void *i_data, size_t i_index, size_t i_total_qty, MPI_Datatype i_type, int i_is_replicated, int i_is_constant) {
  size_t total_reqs = 0;

  if(i_is_constant) {
    if(i_is_replicated) {
      total_reqs = 1;
      modify_data(i_data, i_index, i_total_qty, i_type, total_reqs, rep_a_data); //FIXME: total_reqs==0 ??? 
    } else {    
      if(mall_conf->red_method  == MAM_RED_BASELINE) {
        total_reqs = 1;
      } else if(mall_conf->red_method  == MAM_RED_POINT || mall_conf->red_method  == MAM_RED_RMA_LOCK || mall_conf->red_method  == MAM_RED_RMA_LOCKALL) {
        total_reqs = mall->numC;
      }
      
      modify_data(i_data, i_index, i_total_qty, i_type, total_reqs, dist_a_data);
    }
  } else {
    if(i_is_replicated) {
      modify_data(i_data, i_index, i_total_qty, i_type, total_reqs, rep_s_data);
    } else {
      modify_data(i_data, i_index, i_total_qty, i_type, total_reqs, dist_s_data);
    }
  }
}

/**
 * @brief Return how many entries are available in one of the four registries.
 *
 * @param[in]  i_is_replicated @c MAM_DATA_REPLICATED or @c MAM_DATA_DISTRIBUTED.
 * @param[in]  i_is_constant   @c MAM_DATA_CONSTANT (asynchronous registry) or
 *                             @c MAM_DATA_VARIABLE (synchronous registry).
 * @param[out] o_entries       Receives the amount of registered entries.
 */
void MAM_Data_get_entries(int i_is_replicated, int i_is_constant, size_t *o_entries){
  
  if(i_is_constant) {
    if(i_is_replicated) {
      *o_entries = rep_a_data->entries;
    } else {
      *o_entries = dist_a_data->entries;
    }
  } else {
    if(i_is_replicated) {
      *o_entries = rep_s_data->entries;
    } else {
      *o_entries = dist_s_data->entries;
    }
  }
}

/**
 * @brief Retrieve the buffer and metadata of a registered entry.
 *
 * The returned pointer aliases the buffer held by the registry; the caller must not
 * free it through this function.
 *
 * @param[out] o_data          Receives the pointer to the stored data.
 * @param[in]  i_index         Index of the entry to be read.
 * @param[out] o_total_qty     Receives the amount of elements of the entry;
 *                             ignored when @c NULL.
 * @param[out] o_type          Receives the MPI datatype of the entry;
 *                             ignored when @c NULL.
 * @param[in]  i_is_replicated @c MAM_DATA_REPLICATED or @c MAM_DATA_DISTRIBUTED.
 * @param[in]  i_is_constant   @c MAM_DATA_CONSTANT (asynchronous registry) or
 *                             @c MAM_DATA_VARIABLE (synchronous registry).
 */
void MAM_Data_get_pointer(void **o_data, size_t i_index, size_t *o_total_qty, MPI_Datatype *o_type, int i_is_replicated, int i_is_constant) {
  malleability_data_t *data_struct;

  if(i_is_constant) {
    if(i_is_replicated) {
      data_struct = rep_a_data;
    } else {
      data_struct = dist_a_data;
    }
  } else {
    if(i_is_replicated) {
      data_struct = rep_s_data;
    } else {
      data_struct = dist_s_data;
    }
  }

  *o_data = data_struct->arrays[i_index];
  if(o_total_qty != NULL) *o_total_qty = data_struct->qty[i_index];
  if(o_type != NULL) *o_type = data_struct->types[i_index];
}

/**
 * @brief Return a structure to perform data redistribution during a reconfiguration.
 *
 * This function is intended to be called when the state of MaM is
 * @c MAM_I_USER_PENDING only. It is designed to provide the necessary information
 * for the user to perform data redistribution.
 *
 * @param[out] o_reconf_info Receives the source/target counts, the role of this
 *                           rank and the communicator to use for the redistribution.
 * @return @c MAM_OK if the reconfiguration information was retrieved successfully,
 *         @c MAM_DENIED if the state of MaM is not @c MAM_I_USER_PENDING.
 */
int MAM_Get_Reconf_Info(mam_user_reconf_t *o_reconf_info) {
  if(state != MAM_I_USER_PENDING) return MAM_DENIED;

  *o_reconf_info = *user_reconf;
  return MAM_OK;
}

//======================================================||
//================PRIVATE FUNCTIONS=====================||
//====================MAM STAGES========================||
//======================================================||
//======================================================||
//======================================================||
//======================================================||
//======================================================||
//======================================================||

/**
 * @brief First reconfiguration stage: negotiate the new resources.
 *
 * Resets the timers, starts measuring the reconfiguration and validates the
 * requested configuration. It does not yet consider whether new resources have
 * actually been granted: it simply uses the total amount of targets requested by
 * the user to prepare the reconfiguration.
 *
 * @param[out] o_mam_state Receives @c MAM_NOT_STARTED.
 * @return Always 1, so that the next stage is dispatched immediately.
 */
int MAM_St_rms(int *o_mam_state) {
  reset_malleability_times();
  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->comm);
  #endif
  mall_conf->times->malleability_start = MPI_Wtime();

  MAM_Check_configuration();
  *o_mam_state = MAM_NOT_STARTED;
  state = MAM_I_RMS_COMPLETED;
  mall->wait_targets_posted = 0;

  //if(CHECK_RMS()) {return MAM_DENIED;}    
  return 1;
}

/**
 * @brief Second reconfiguration stage: perform or start the spawn.
 *
 * Records the current group size as the parent size and launches the spawn, which
 * may complete inmediately or continue in the background when an asynchronous spawn
 * strategy is configured. Sources that will not survive the reconfiguration are
 * flagged as zombies here: with the Merge method those are the ranks beyond the
 * requested target count, and with the Baseline method every source.
 *
 * @return 1 if the spawn already finished (or was postponed) and the state machine
 *         can advance immediately, 0 if the spawn is still pending.
 */
int MAM_St_spawn_start(void) {
  mall->num_parents = mall->numP;
  state = spawn_step();
  //FIXME: This is needed but ugly
  if(mall_conf->spawn_method == MAM_SPAWN_MERGE && mall->myId >= mall->numC){ mall->zombie = 1; }
  else if(mall_conf->spawn_method == MAM_SPAWN_BASELINE){ mall->zombie = 1; }

  if (state == MAM_I_SPAWN_COMPLETED || state == MAM_I_SPAWN_ADAPT_POSTPONE){
    return 1;
  }
  return 0;
}


/**
 * @brief Third reconfiguration stage: check whether an asynchronous spawn finished.
 *
 * Only reached when the spawn was started in the background; it is never called for
 * a synchronous spawn. Records the spawn time as soon as the children are available.
 *
 * @param[in] i_wait_completed @c MAM_WAIT_COMPLETION to block until the spawn ends,
 *                             @c MAM_CHECK_COMPLETION to only test it.
 * @return 1 if the spawn completed and the state machine can advance, 0 otherwise.
 */
int MAM_St_spawn_pending(int i_wait_completed) {
  state = check_spawn_state(&(mall->intercomm), mall->comm, i_wait_completed);
  if (state == MAM_I_SPAWN_COMPLETED || state == MAM_I_SPAWN_ADAPTED) {
    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->comm);
    #endif
    mall_conf->times->spawn_time = MPI_Wtime() - mall_conf->times->malleability_start;
    return 1;
  }
  return 0;
}

/**
 * @brief Fourth reconfiguration stage: start the asynchronous data redistribution.
 *
 * Chooses the root used for the collective operations towards the targets. When the
 * spawn keeps an intercommunicator, the collectives must use @c MPI_ROOT on the
 * actual root and @c MPI_PROC_NULL elsewhere; otherwise the plain root rank is used.
 * Then it starts sending the constant (asynchronous) data, if there is any.
 *
 * @return Always 1, so that the next stage is dispatched immediately.
 */
int MAM_St_red_start(void) {
  if(MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_INTERCOMM, NULL)) {
    mall->root_collectives = mall->myId == mall->root ? MPI_ROOT : MPI_PROC_NULL;
  } else {
    mall->root_collectives = mall->root;
  }

  state = start_redistribution();
  return 1;
}

/**
 * @brief Fourth reconfiguration stage (continued): poll the asynchronous transfers.
 *
 * If an asynchronous data redistribution was started, checks its state and advances
 * to the user stage once it has finished. The check is delegated to the background
 * thread when the pthread redistribution strategy is in use, and to the request-based
 * path otherwise.
 *
 * @param[in] i_wait_completed @c MAM_WAIT_COMPLETION to block until the transfers
 *                             end, @c MAM_CHECK_COMPLETION to only test them.
 * @return 1 if the transfers completed and the state machine can advance, 0 otherwise.
 */
int MAM_St_red_pending(int i_wait_completed) {
  if(MAM_Contains_strat(MAM_RED_STRATEGIES, MAM_STRAT_RED_PTHREAD, NULL)) {
    state = thread_check(i_wait_completed);
  } else {
    state = check_redistribution(i_wait_completed);
  }

  if(state != MAM_I_DIST_PENDING) { 
    state = MAM_I_USER_START;
    return 1;
  }
  return 0;
}

/**
 * @brief Fifth reconfiguration stage: prepare the call to the user callback.
 *
 * Builds the temporary communicator handed to the application: sources and targets
 * are merged when the spawn produced an intercommunicator, otherwise the existing
 * communicator is duplicated.
 *
 * @param[out] o_mam_state Receives @c MAM_USER_PENDING.
 * @return Always 1, so that the next stage is dispatched immediately.
 *
 * @todo FIXME: This assumes a user callback exists; when there is none the time
 *       spent preparing the communicator is wasted.
 */
int MAM_St_user_start(int *o_mam_state) {
  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->intercomm);
  #endif
  mall_conf->times->user_start = MPI_Wtime(); // Timestamp of when the user redistribution starts
  if(MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_INTERCOMM, NULL)) {
    MPI_Intercomm_merge(mall->intercomm, MAM_SOURCES, &mall->tmp_comm); //The group passing 0 is placed first
  } else {
    MPI_Comm_dup(mall->intercomm, &mall->tmp_comm);
  }
  MPI_Comm_set_name(mall->tmp_comm, "MAM_USER_TMP");
  state = MAM_I_USER_PENDING;
  *o_mam_state = MAM_USER_PENDING;
  return 1;
}

/**
 * @brief Sixth reconfiguration stage: let the user redistribute its own data.
 *
 * Calls the user callback so that the application redistributes whatever MaM does
 * not manage. If there is no callback, the stage is skipped straight away. When a
 * callback exists, the stage is only considered finished once the user calls
 * ::MAM_Resume_redistribution.
 *
 * @param[out] o_mam_state      Forwarded to ::MAM_Resume_redistribution when there
 *                              is no user callback.
 * @param[in]  i_wait_completed @c MAM_WAIT_COMPLETION to keep invoking the callback
 *                              until the user resumes, @c MAM_CHECK_COMPLETION to
 *                              invoke it only once per checkpoint.
 * @param[in]  i_user_function  User callback; may be @c NULL.
 * @param[in]  i_user_args      Opaque argument forwarded to @p i_user_function.
 * @return 1 if the user phase finished and the state machine can advance, 0 otherwise.
 */
int MAM_St_user_pending(int *o_mam_state, int i_wait_completed, void (*i_user_function)(void *), void *i_user_args) {
  #if MAM_DEBUG
    if(mall->myId == mall->root) { DEBUG_FUNC("Starting USER redistribution", mall->myId, mall->numP); fflush(stdout); }
  #endif
  if(i_user_function != NULL) {
    MAM_I_create_user_struct(MAM_SOURCES);
    do {
      i_user_function(i_user_args);
    } while(i_wait_completed && state == MAM_I_USER_PENDING);
  } else {
    MAM_Resume_redistribution(o_mam_state);
  }

  if(state != MAM_I_USER_PENDING) {
    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->intercomm);
    #endif
    if(mall_conf->spawn_method == MAM_SPAWN_MERGE) mall_conf->times->user_end = MPI_Wtime(); // Timestamp of when the user redistribution ends
    #if MAM_DEBUG
      if(mall->myId == mall->root) { DEBUG_FUNC("Ended USER redistribution", mall->myId, mall->numP); fflush(stdout); }
    #endif
    return 1;
  }
  return 0;
}

/**
 * @brief Seventh reconfiguration stage: perform the synchronous data redistribution.
 *
 * @return Always 1, so that the next stage is dispatched immediately.
 */
int MAM_St_user_completed(void) {
  state = end_redistribution();
  return 1;
}

/**
 * @brief Eighth reconfiguration stage: finish a Merge shrink adaptation.
 *
 * Only invoked when the Merge spawn method is used in a shrink operation. It clears
 * the postpone flag and completes the spawn, which in this case means splitting the
 * group so that the surplus sources can leave. The wait mode is forced to
 * @c MAM_WAIT_COMPLETION because the operation cannot be left pending here.
 *
 * @param[in] i_wait_completed Ignored; overwritten with @c MAM_WAIT_COMPLETION.
 * @return Always 1, so that the next stage is dispatched immediately.
 */
int MAM_St_spawn_adapt_pending(int i_wait_completed) {
  i_wait_completed = MAM_WAIT_COMPLETION;
  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->comm);
  #endif
  mall_conf->times->spawn_start = MPI_Wtime();
  unset_spawn_postpone_flag(state);
  state = check_spawn_state(&(mall->intercomm), mall->comm, i_wait_completed);
/* TODO: Document the problem; essentially, it is not possible in the current form.
 * Moreover, it only concerns an operation that we have measured as "extremely" fast.
 * It is NOT possible to do it at this point because it can only be done after sending
 * the asynchronous data, and therefore that data would lose its validity if more
 * iterations were performed.
 * For this reason, Merge+Shrink does not support threading for the spawn.
  if(!MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_PTHREAD, NULL)) {
    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->comm);
    #endif
    mall_conf->times->spawn_time = MPI_Wtime() - mall_conf->times->spawn_start;
    return 1;
  }
  return 0;
  */
  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->comm);
  #endif
  mall_conf->times->spawn_time = MPI_Wtime() - mall_conf->times->spawn_start;
  return 1;
}

/**
 * @brief Ninth reconfiguration stage: terminate the reconfiguration.
 *
 * @param[out] o_mam_state Receives @c MAM_COMPLETED through ::MAM_Commit.
 * @return Always 0, since the state machine has nothing left to dispatch.
 */
int MAM_St_completed(int *o_mam_state) {
  MAM_Commit(o_mam_state);
  return 0;
}


//======================================================||
//================PRIVATE FUNCTIONS=====================||
//=====================CHILDREN=========================||
//======================================================||
//======================================================||
//======================================================||
//======================================================||
//======================================================||
//======================================================||
/**
 * @brief Initialise the data of the spawned children.
 *
 * The children connect to their parents (the sources) and receive from them the
 * configuration of the execution to be performed, followed by the data itself,
 * either asynchronously, synchronously or both. The asynchronous (constant) data is
 * received first, then the user callback is given the chance to redistribute its own
 * data, and finally the synchronous (variable) data is received. The function ends by
 * committing the reconfiguration, after which the children are ready to run the
 * application.
 *
 * @param[in] i_user_function Optional user callback for the user redistribution phase.
 * @param[in] i_user_args     Opaque argument forwarded to @p i_user_function.
 */
void Children_init(void (*i_user_function)(void *), void *i_user_args) {
  size_t i;

  #if MAM_DEBUG
    DEBUG_FUNC("MaM will now initialize spawned processes", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif

  malleability_connect_children(&(mall->intercomm));
  if(mall_conf->spawn_method == MAM_SPAWN_MERGE) { // For Merge Method, these processes will be added
    MPI_Comm_rank(mall->intercomm, &mall->myId);
    MPI_Comm_size(mall->intercomm, &mall->numP);
  }
  mall->root_collectives = mall->root_parents;

  if(MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_MULTIPLE, NULL)
    || MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_PARALLEL, NULL)) {
    mall->internode_group = 0;
  } else {
    mall->internode_group = MAM_Is_internode_group();
  }

  #if MAM_DEBUG
    DEBUG_FUNC("Spawned have completed spawn step", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif

  comm_data_info(rep_a_data, dist_a_data, MAM_TARGETS);
  if(dist_a_data->entries || rep_a_data->entries) { // Receive asynchronous data
    #if MAM_DEBUG >= 2
      DEBUG_FUNC("Spawned start asynchronous redistribution", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
    #endif
    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->intercomm);
    #endif

    if(MAM_Contains_strat(MAM_RED_STRATEGIES, MAM_STRAT_RED_PTHREAD, NULL)) {
      recv_data(mall->num_parents, dist_a_data, MAM_USE_SYNCHRONOUS);
      for(i=0; i<rep_a_data->entries; i++) {
        MPI_Bcast(rep_a_data->arrays[i], rep_a_data->qty[i], rep_a_data->types[i], mall->root_collectives, mall->intercomm);
      } 
    } else {
      recv_data(mall->num_parents, dist_a_data, MAM_USE_ASYNCHRONOUS); 

      for(i=0; i<rep_a_data->entries; i++) {
        MPI_Ibcast(rep_a_data->arrays[i], rep_a_data->qty[i], rep_a_data->types[i], mall->root_collectives, mall->intercomm, &(rep_a_data->requests[i][0]));
      } 
      #if MAM_DEBUG >= 2
        DEBUG_FUNC("Spawned started asynchronous redistribution", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
      #endif

      for(i=0; i<rep_a_data->entries; i++) {
        async_communication_wait(rep_a_data->requests[i], rep_a_data->request_qty[i]);
      }
      for(i=0; i<dist_a_data->entries; i++) {
        async_communication_wait(dist_a_data->requests[i], dist_a_data->request_qty[i]);
      }
      if(MAM_Contains_strat(MAM_RED_STRATEGIES, MAM_STRAT_RED_WAIT_TARGETS, NULL)) {
        MPI_Ibarrier(mall->intercomm, &mall->wait_targets);
        mall->wait_targets_posted = 1;
        MPI_Wait(&mall->wait_targets, MPI_STATUS_IGNORE);
      }

      #if MAM_DEBUG >= 2
        DEBUG_FUNC("Spawned waited for all asynchronous redistributions", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
      #endif
      for(i=0; i<dist_a_data->entries; i++) {
        async_communication_end(dist_a_data->requests[i], dist_a_data->request_qty[i], &(dist_a_data->windows[i]), &dist_a_data->idS[i*2]);
      }
      free(dist_a_data->idS); dist_a_data->idS = NULL;
      for(i=0; i<rep_a_data->entries; i++) {
        async_communication_end(rep_a_data->requests[i], rep_a_data->request_qty[i], &(rep_a_data->windows[i]), &rep_a_data->idS[i*2]);
      }
    }

    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->intercomm);
    #endif
    mall_conf->times->async_end= MPI_Wtime(); // Timestamp of when the asynchronous communication ends
  }
  #if MAM_DEBUG
    DEBUG_FUNC("Spawned have completed asynchronous data redistribution step", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif

  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->intercomm);
  #endif
  if(MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_INTERCOMM, NULL)) {
    MPI_Intercomm_merge(mall->intercomm, MAM_TARGETS, &mall->tmp_comm); //The group passing 0 is placed first
  } else {
    MPI_Comm_dup(mall->intercomm, &mall->tmp_comm);
  }
  MPI_Comm_set_name(mall->tmp_comm, "MAM_USER_TMP");
  if(i_user_function != NULL) {
    state = MAM_I_USER_PENDING;
    MAM_I_create_user_struct(MAM_TARGETS);
    i_user_function(i_user_args);
  }
  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->intercomm);
  #endif
  mall_conf->times->user_end = MPI_Wtime(); // Timestamp of when the user redistribution ends

  #if MAM_DEBUG >= 2
      DEBUG_FUNC("Spawned start synchronous redistribution", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
    #endif
  comm_data_info(rep_s_data, dist_s_data, MAM_TARGETS);
  if(dist_s_data->entries || rep_s_data->entries) { // Receive synchronous data
    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->intercomm);
    #endif
    recv_data(mall->num_parents, dist_s_data, MAM_USE_SYNCHRONOUS);

    for(i=0; i<rep_s_data->entries; i++) {
      MPI_Bcast(rep_s_data->arrays[i], rep_s_data->qty[i], rep_s_data->types[i], mall->root_collectives, mall->intercomm);
    } 
    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->intercomm);
    #endif
    mall_conf->times->sync_end = MPI_Wtime(); // Timestamp of when the synchronous communication ends
  }
  #if MAM_DEBUG
    DEBUG_FUNC("Targets have completed synchronous data redistribution step", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif

  MAM_Commit(NULL);

  #if MAM_DEBUG
    DEBUG_FUNC("MaM has been initialized correctly for new ranks", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif
}

//======================================================||
//================PRIVATE FUNCTIONS=====================||
//=====================PARENTS==========================||
//======================================================||
//======================================================||
//======================================================||
//======================================================||

/**
 * @brief Create the children processes.
 *
 * Starts the spawn on the dedicated thread communicator and records the spawn time
 * unless the spawn runs in a background thread, in which case the time is recorded
 * later by ::MAM_St_spawn_pending. If the creation was requested in the background,
 * the current state is returned instead of the completed one.
 *
 * @return The malleability state resulting from the spawn attempt.
 */
int spawn_step(void){
  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->comm);
  #endif
  mall_conf->times->spawn_start = MPI_Wtime();
 
  state = init_spawn(mall->thread_comm, &(mall->intercomm));

  if(!MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_PTHREAD, NULL)) {
      #if MAM_USE_BARRIERS
        MPI_Barrier(mall->comm);
      #endif
      mall_conf->times->spawn_time = MPI_Wtime() - mall_conf->times->malleability_start;
  }
  return state;
}


/**
 * @brief Begin the data redistribution towards the new group of processes.
 *
 * First the configuration to be used is sent to the new group of processes, and then
 * the asynchronous and/or synchronous transfers are issued if there are any.
 *
 * If there is asynchronous communication, it is started and the function returns
 * indicating that an asynchronous send is in progress. Depending on the configured
 * strategy the transfer is either issued as non-blocking requests, or delegated to a
 * background thread through thread_creation().
 *
 * If there is no asynchronous communication, the state machine moves directly to the
 * user stage, from which the synchronous transfers will eventually be performed.
 *
 * @return @c MAM_I_DIST_PENDING while asynchronous transfers are in flight, or
 *         @c MAM_I_USER_START when there is no asynchronous data to send.
 */
int start_redistribution(void) {
  size_t i;

  if(mall->intercomm == MPI_COMM_NULL) {
    // Having no communicator means the spawn was postponed,
    //   which corresponds to the Merge Shrink spawn
    MPI_Comm_dup(mall->comm, &(mall->intercomm));
  }

  comm_data_info(rep_a_data, dist_a_data, MAM_SOURCES);
  if(dist_a_data->entries || rep_a_data->entries) { // Send asynchronous data
    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->intercomm);
    #endif
    mall_conf->times->async_start = MPI_Wtime();
    if(MAM_Contains_strat(MAM_RED_STRATEGIES, MAM_STRAT_RED_PTHREAD, NULL)) {
      return thread_creation();
    } else {
      send_data(mall->numC, dist_a_data, MAM_USE_ASYNCHRONOUS);
      for(i=0; i<rep_a_data->entries; i++) {
        MPI_Ibcast(rep_a_data->arrays[i], rep_a_data->qty[i], rep_a_data->types[i], mall->root_collectives, mall->intercomm, &(rep_a_data->requests[i][0]));
      } 

      if(mall->zombie && MAM_Contains_strat(MAM_RED_STRATEGIES, MAM_STRAT_RED_WAIT_TARGETS, NULL)) {
        MPI_Ibarrier(mall->intercomm, &mall->wait_targets);
        mall->wait_targets_posted = 1;
      } 
      return MAM_I_DIST_PENDING; 
    }
  } 
  return MAM_I_USER_START;
}


/**
 * @brief Check whether the asynchronous redistribution has finished.
 *
 * If it has not finished, the function reports so; otherwise the asynchronous
 * communications are closed (releasing requests and RMA windows) and the state
 * machine moves on to the user stage.
 *
 * This function supports two ways of deciding when the asynchronous communication is
 * considered finished. With the request-based strategies, it is considered finished
 * once the sources have finished sending. With the "wait targets" strategy, an
 * @c MPI_Ibarrier with the targets is used instead, so it is considered finished once
 * the children have finished receiving.
 *
 * @param[in] i_wait_completed @c MAM_WAIT_COMPLETION to block until every transfer
 *                             ends, @c MAM_CHECK_COMPLETION to test them and reach a
 *                             global decision with an allreduce over the sources.
 * @return @c MAM_I_DIST_PENDING if the transfers are still in flight, or
 *         @c MAM_I_USER_START once they have all completed.
 */
int check_redistribution(int i_wait_completed) {
  int completed, local_completed, all_completed;
  size_t i, req_qty;
  MPI_Request *req_completed;
  MPI_Win window;
  local_completed = 1;
  #if MAM_DEBUG >= 2
    DEBUG_FUNC("Sources are testing for all asynchronous redistributions", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif

  if(i_wait_completed) {
    if(MAM_Contains_strat(MAM_RED_STRATEGIES, MAM_STRAT_RED_WAIT_TARGETS, NULL) && !mall->wait_targets_posted) {
      MPI_Ibarrier(mall->intercomm, &mall->wait_targets);
      mall->wait_targets_posted = 1;
    }
    for(i=0; i<dist_a_data->entries; i++) {
      req_completed = dist_a_data->requests[i];
      req_qty = dist_a_data->request_qty[i];
      async_communication_wait(req_completed, req_qty);
    }
    for(i=0; i<rep_a_data->entries; i++) {
      req_completed = rep_a_data->requests[i];
      req_qty = rep_a_data->request_qty[i];
      async_communication_wait(req_completed, req_qty);
    }

    if(MAM_Contains_strat(MAM_RED_STRATEGIES, MAM_STRAT_RED_WAIT_TARGETS, NULL)) { MPI_Wait(&mall->wait_targets, MPI_STATUS_IGNORE); }
  } else {
    if(mall->wait_targets_posted) { 
      MPI_Test(&mall->wait_targets, &local_completed, MPI_STATUS_IGNORE); 
    } else {
      for(i=0; i<dist_a_data->entries; i++) {
        req_completed = dist_a_data->requests[i];
        req_qty = dist_a_data->request_qty[i];
        completed = async_communication_check(MAM_SOURCES, req_completed, req_qty);
        local_completed = local_completed && completed;
      }
      for(i=0; i<rep_a_data->entries; i++) {
        req_completed = rep_a_data->requests[i];
        req_qty = rep_a_data->request_qty[i];
        completed = async_communication_check(MAM_SOURCES, req_completed, req_qty);
        local_completed = local_completed && completed;
      }

      if(local_completed && MAM_Contains_strat(MAM_RED_STRATEGIES, MAM_STRAT_RED_WAIT_TARGETS, NULL)) {
        MPI_Ibarrier(mall->intercomm, &mall->wait_targets);
        mall->wait_targets_posted = 1;
        MPI_Test(&mall->wait_targets, &local_completed, MPI_STATUS_IGNORE); //TODO: Figure out if last process takes profit from calling here
      }
    }
    #if MAM_DEBUG >= 2
      DEBUG_FUNC("Sources will now check a global decision", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
    #endif

    MPI_Allreduce(&local_completed, &all_completed, 1, MPI_INT, MPI_MIN, mall->comm);
    if(!all_completed) return MAM_I_DIST_PENDING; // Continue only if asynchronous send has ended 
  }

  #if MAM_DEBUG >= 2
    DEBUG_FUNC("Sources sent asynchronous redistributions", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif

  for(i=0; i<dist_a_data->entries; i++) {
    req_completed = dist_a_data->requests[i];
    req_qty = dist_a_data->request_qty[i];
    window = dist_a_data->windows[i];
    async_communication_end(req_completed, req_qty, &window, &dist_a_data->idS[i*2]);
  }
  free(dist_a_data->idS); dist_a_data->idS = NULL;
  for(i=0; i<rep_a_data->entries; i++) {
    req_completed = rep_a_data->requests[i];
    req_qty = rep_a_data->request_qty[i];
    window = rep_a_data->windows[i];
    async_communication_end(req_completed, req_qty, &window, &rep_a_data->idS[i*2]);
  }

  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->intercomm);
  #endif
  if(mall_conf->spawn_method == MAM_SPAWN_MERGE) mall_conf->times->async_end = MPI_Wtime(); // Merge method only
  return MAM_I_USER_START;
}

/**
 * @brief Finish the data redistribution towards the children.
 *
 * Performs the synchronous (variable) communications if there are any: the
 * distributed entries are sent with send_data() and the replicated ones are
 * broadcast to the targets.
 *
 * @return @c MAM_I_SPAWN_ADAPT_PENDING when a Merge shrink still has to split the
 *         group, or @c MAM_I_DIST_COMPLETED otherwise.
 */ 
int end_redistribution(void) {
  size_t i;
  int local_state;

  #if MAM_DEBUG
    DEBUG_FUNC("Sources have started synchronous data redistribution step", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(mall->comm);
  #endif
  comm_data_info(rep_s_data, dist_s_data, MAM_SOURCES);
  if(dist_s_data->entries || rep_s_data->entries) { // Send synchronous data
    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->intercomm);
    #endif
    mall_conf->times->sync_start = MPI_Wtime();
    send_data(mall->numC, dist_s_data, MAM_USE_SYNCHRONOUS);

    for(i=0; i<rep_s_data->entries; i++) {
      MPI_Bcast(rep_s_data->arrays[i], rep_s_data->qty[i], rep_s_data->types[i], mall->root_collectives, mall->intercomm);
    }

    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->intercomm);
    #endif
    if(mall_conf->spawn_method == MAM_SPAWN_MERGE) mall_conf->times->sync_end = MPI_Wtime(); // Merge method only
  }
  #if MAM_DEBUG
    DEBUG_FUNC("Sources have completed synchronous data redistribution step", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(mall->comm);
  #endif

  local_state = MAM_I_DIST_COMPLETED;
  if(mall_conf->spawn_method == MAM_SPAWN_MERGE && mall->numP > mall->numC) { // Merge Shrink
    local_state = MAM_I_SPAWN_ADAPT_PENDING;
  }

  return local_state;
}

// TODO: Move to another file??
//======================================================||
//================PRIVATE FUNCTIONS=====================||
//===============COMM PARENTS THREADS===================||
//======================================================||
//======================================================||


/**
 * @brief State of the background communication carried out by the auxiliary thread.
 *
 * Set to @c MAM_I_DIST_PENDING when the thread is created and to
 * @c MAM_I_DIST_COMPLETED once it has sent everything.
 *
 * @todo FIXME: Use a handler instead of a file-global variable.
 */
int comm_state;

/**
 * @brief Create a thread to carry out a communication in the background.
 *
 * The thread runs thread_async_work(), which performs blocking transfers that the
 * application perceives as happening in the background.
 *
 * @return @c MAM_I_DIST_PENDING if the thread was created, or -1 on failure (after
 *         aborting the MPI execution).
 */
int thread_creation(void) {
  comm_state = MAM_I_DIST_PENDING;
  if(pthread_create(&(mall->async_thread), NULL, thread_async_work, NULL)) {
    printf("Error al crear el hilo\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    return -1;
  }
  return comm_state;
}

/**
 * @brief Check from the master thread whether the auxiliary thread has finished.
 *
 * When not waiting for completion, all the sources agree on whether every auxiliary
 * thread has finished its distribution before joining it, since the join itself is
 * blocking.
 *
 * @param[in] i_wait_completed @c MAM_WAIT_COMPLETION to join the thread directly, or
 *                             @c MAM_CHECK_COMPLETION to first reach a global
 *                             decision among the sources.
 * @return @c MAM_I_DIST_PENDING if some source has not finished yet,
 *         @c MAM_I_USER_START once the thread has been joined, or -2 if the join
 *         failed (after aborting the MPI execution).
 */
int thread_check(int i_wait_completed) {
  int all_completed = 0;

  if(!i_wait_completed) {
    // Check that every thread has finished the distribution (same value in commAsync)
    MPI_Allreduce(&comm_state, &all_completed, 1, MPI_INT, MPI_MAX, mall->comm);
    if(all_completed != MAM_I_DIST_COMPLETED) return MAM_I_DIST_PENDING; // Continue only if asynchronous send has ended 
  }


  if(pthread_join(mall->async_thread, NULL)) {
    printf("Error al esperar al hilo\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    return -2;
  } 

  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->intercomm);
  #endif
  if(mall_conf->spawn_method == MAM_SPAWN_MERGE) mall_conf->times->async_end = MPI_Wtime(); // Merge method only
  return MAM_I_USER_START;
}


/**
 * @brief Body executed by the auxiliary thread.
 *
 * Performs a synchronous communication with the children which, from the point of
 * view of the user, can be considered as happening in the background. Once the
 * communication ends, the master thread can detect it through @c comm_state.
 *
 * @return Never returns a value; the thread terminates with @c pthread_exit.
 */
void* thread_async_work() {
  size_t i;

  send_data(mall->numC, dist_a_data, MAM_USE_SYNCHRONOUS);
  for(i=0; i<rep_a_data->entries; i++) {
    MPI_Bcast(rep_a_data->arrays[i], rep_a_data->qty[i], rep_a_data->types[i], mall->root_collectives, mall->intercomm);
  } 
  comm_state = MAM_I_DIST_COMPLETED;
  pthread_exit(NULL);
}


//==============================================================================

/**
 * @brief Build the structure handed to the user to help with its data reconfiguration.
 *
 * Fills the global ::user_reconf snapshot with the temporary communicator, the source
 * and target counts, and the role of this rank. Children always report
 * @c MAM_PROC_NEW_RANK; sources report @c MAM_PROC_ZOMBIE when they will not survive
 * the reconfiguration and @c MAM_PROC_CONTINUE when they will.
 *
 * @param[in] i_is_children_group Non-zero (@c MAM_TARGETS) when called by the newly
 *                                spawned children, zero (@c MAM_SOURCES) when called
 *                                by the sources.
 */
void MAM_I_create_user_struct(int i_is_children_group) {
  user_reconf->comm = mall->tmp_comm;

  if(i_is_children_group) {
    user_reconf->rank_state = MAM_PROC_NEW_RANK;
    user_reconf->numS = mall->num_parents;
    user_reconf->numT = mall->numP;
  } else {
    user_reconf->numS = mall->numP;
    user_reconf->numT = mall->numC;
    if(mall->zombie) user_reconf->rank_state = MAM_PROC_ZOMBIE;
    else user_reconf->rank_state = MAM_PROC_CONTINUE;
  }
}
