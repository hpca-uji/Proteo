#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <mpi.h>
#include <string.h>
#include "MAM_Constants.h"
#include "MAM_DataStructures.h"
#include "MAM_Configuration.h"
#include "ProcessDist.h"
#include "GenericSpawn.h"
#include "Baseline.h"
#include "Merge.h"
#include "Spawn_state.h"

/**
 * @file GenericSpawn.c
 * @brief Implementation of the generic (method-agnostic) spawn orchestration.
 *
 * @note This code is a Singleton object: only one instance (::spawn_data) can
 *       be used at a given time, and no multiple calls to perform different
 *       reconfigurations can be in progress at the same time.
 */

Spawn_data *spawn_data = NULL;
pthread_t spawn_thread;

//--------------PRIVATE CONFIGURATION DECLARATIONS---------------//

/**
 * @brief Build the global ::spawn_data configuration for a sources-side spawn.
 *
 * Allocates and fills the singleton ::spawn_data used by the rest of this
 * module. In particular:
 * - @c spawn_is_single / @c spawn_is_async / @c spawn_is_intercomm /
 *   @c spawn_is_multiple / @c spawn_is_parallel are read from
 *   @c MAM_SPAWN_STRATEGIES (@c mam_spawn_strategies) and enable the Single,
 *   threaded-async, Intercomm, Multiple and Parallel strategies respectively.
 * - @c mapping_fill_method is fixed to @c MAM_PHY_TYPE_HOSTFILE (hostfile-based
 *   physical mapping); @c MAM_PHY_TYPE_STRING is not used here.
 * - Under Baseline, @c spawn_qty equals @c target_qty and @c already_created
 *   is @c 0: every target process is newly spawned (no sources are reused).
 * - Under Merge, @c spawn_qty is @c target_qty @c - @c initial_qty (only the
 *   growth is spawned) and @c already_created is @c initial_qty: the sources
 *   are reused as targets and only the difference in process count needs to
 *   be created.
 *
 * If the spawn is asynchronous, also initialises the spawn-state
 * synchronisation primitives (::init_spawn_state).
 *
 * @param[in] i_comm Communicator among the sources requesting the spawn.
 */
void set_spawn_configuration(MPI_Comm i_comm);

/**
 * @brief Free the global ::spawn_data structure and everything it owns.
 *
 * Releases the @c MPI_Info mapping of every spawn set, the @c sets array
 * itself, and, if the spawn was asynchronous, the spawn-state synchronisation
 * primitives (::free_spawn_state). Safe to call when ::spawn_data is already
 * @c NULL.
 */
void deallocate_spawn_data(void);

//--------------PRIVATE DECLARATIONS---------------//

/**
 * @brief Generic process-creation step, dispatching to Baseline or Merge.
 *
 * When there are processes left to spawn (@c spawn_data->spawn_qty @c > @c 0),
 * computes and broadcasts the physical distribution of the new children
 * (::processes_dist) before creating them. When @p i_data_stage is
 * @c MAM_I_DIST_COMPLETED, also frees the logical occupancy of the ranks
 * being removed (::remove_dist), used by the Merge shrink path once data
 * redistribution has finished.
 *
 * Runs the configured spawn method (Baseline or Merge) and, on completion,
 * updates the global spawn state (::set_spawn_state), except when the state
 * read back after running the method is @c MAM_I_SPAWN_PENDING while the
 * value just computed is @c MAM_I_SPAWN_ADAPT_POSTPONE, in which case the
 * update is skipped to avoid overwriting a more advanced state.
 *
 * @param[in,out] io_child     Receives the resulting communicator to the new
 *                               group of processes (see ::baseline / ::merge).
 * @param[in]     i_data_stage Data-redistribution stage (@c mam_inner_states):
 *                               @c MAM_I_NOT_STARTED for the initial spawn
 *                               attempt, @c MAM_I_DIST_COMPLETED once data
 *                               redistribution has completed (Merge shrink).
 */
void generic_spawn(MPI_Comm *io_child, int i_data_stage);

/**
 * @brief Check whether the asynchronous Single-strategy spawn step has finished.
 *
 * On the root process, optionally blocks (@p i_wait_completed) until the
 * auxiliary thread reports completion; the resulting state is then broadcast
 * to the rest of the sources. Non-root processes must join here regardless,
 * both to finalise the spawn and if the application has otherwise ended its
 * work.
 *
 * If the step has completed (@c MAM_I_SPAWN_SINGLE_COMPLETED), the state is
 * advanced to @c MAM_I_SPAWN_PENDING and non-root processes create their own
 * auxiliary thread (::allocate_thread_spawn) to continue with the generic
 * spawn step. Otherwise @c MAM_I_SPAWN_SINGLE_PENDING is kept.
 *
 * @param[in]     i_comm           Communicator among the sources, used to
 *                                  broadcast the resulting state from root.
 * @param[in,out] io_child         Forwarded to ::allocate_thread_spawn for
 *                                  non-root processes once the step completes.
 * @param[in]     i_global_state   Current known state
 *                                  (@c MAM_I_SPAWN_SINGLE_PENDING or
 *                                  @c MAM_I_SPAWN_SINGLE_COMPLETED).
 * @param[in]     i_wait_completed Non-zero to block (root only) until the
 *                                  auxiliary thread reports completion.
 * @return Resulting state: @c MAM_I_SPAWN_SINGLE_PENDING if still in
 *         progress, or @c MAM_I_SPAWN_PENDING once the Single step has
 *         completed.
 */
int check_single_state(MPI_Comm i_comm, MPI_Comm *io_child, int i_global_state, int i_wait_completed);

/**
 * @brief Check whether the asynchronous generic spawn step has finished for all sources.
 *
 * Optionally blocks (@p i_wait_completed) on this rank's auxiliary thread,
 * then reduces the local state across all sources with @c MPI_MIN so that the
 * step is only reported as finished once every source's thread has completed.
 * If the combined state is @c MAM_I_SPAWN_COMPLETED or @c MAM_I_SPAWN_ADAPTED,
 * also updates the global spawn state accordingly.
 *
 * @param[in] i_comm           Communicator among the sources, used for the
 *                              @c MPI_Allreduce state reduction.
 * @param[in] i_local_state    Current known local state for this rank
 *                              (@c MAM_I_SPAWN_PENDING, @c MAM_I_SPAWN_COMPLETED
 *                              or @c MAM_I_SPAWN_ADAPTED).
 * @param[in] i_wait_completed Non-zero to block until the local auxiliary
 *                              thread reports completion before reducing.
 * @return Combined state across all sources: @c MAM_I_SPAWN_PENDING if any
 *         source is still in progress, otherwise @c MAM_I_SPAWN_COMPLETED or
 *         @c MAM_I_SPAWN_ADAPTED.
 */
int check_generic_state(MPI_Comm i_comm, int i_local_state, int i_wait_completed);

//--------------PRIVATE THREADS DECLARATIONS---------------//

/**
 * @brief Create and detach the auxiliary thread that performs asynchronous spawn work.
 *
 * The thread cannot be joined; it detaches itself immediately and releases
 * its own resources once ::thread_work finishes.
 *
 * @param[in,out] io_child Communicator pointer forwarded to ::thread_work /
 *                          ::generic_spawn; receives the resulting
 *                          communicator once the thread completes its work.
 * @return @c 0 on success; @c -1 if thread creation or detaching failed (the
 *         MPI job is aborted before returning in that case).
 */
int allocate_thread_spawn(MPI_Comm *io_child);

/**
 * @brief Auxiliary-thread entry point that performs the spawn work in the background.
 *
 * Runs ::generic_spawn to configure/create the new group of processes. If the
 * result is @c MAM_I_SPAWN_ADAPT_POSTPONE or @c MAM_I_SPAWN_PENDING (Merge
 * shrink awaiting data redistribution), blocks on ::wait_redistribution and
 * then finishes the process-creation step with a second ::generic_spawn call
 * (@c MAM_I_DIST_COMPLETED). Finally wakes up the master thread via
 * ::wakeup_completion and terminates.
 *
 * @param[in,out] io_args Communicator pointer (@c MPI_Comm @c *), passed as
 *                          @c void @c * per the pthread API; forwarded to
 *                          ::generic_spawn to receive the resulting
 *                          communicator.
 * @return Always @c NULL (the thread exits via @c pthread_exit).
 */
void* thread_work(void *io_args);


//--------------PUBLIC FUNCTIONS---------------//

/**
 * @brief Request creation of a new group of @c target_qty processes (sources side).
 *
 * Builds the spawn configuration from @p i_comm and, depending on whether the
 * configured strategy is synchronous or asynchronous (@c spawn_is_async):
 * - Synchronous: creates the processes immediately (blocking call).
 * - Asynchronous: starts an auxiliary thread that configures/creates them in
 *   the background; ::check_spawn_state must be called afterwards to
 *   retrieve the result.
 *
 * @param[in]     i_comm   Communicator among the sources requesting the spawn.
 * @param[in,out] io_child Receives the intercommunicator (or, under Merge,
 *                          the merged/split communicator) once ready. Only
 *                          fully populated when the returned state is
 *                          @c MAM_I_SPAWN_COMPLETED; otherwise
 *                          ::check_spawn_state must be called.
 * @return Spawn state (@c mam_inner_states). If different from
 *         @c MAM_I_SPAWN_COMPLETED, ::check_spawn_state must be called to
 *         finish the operation.
 */
int init_spawn(MPI_Comm i_comm, MPI_Comm *io_child) {
  int local_state;
  set_spawn_configuration(i_comm);
  if(spawn_data->target_qty == 0) { return MAM_I_SPAWN_COMPLETED; }

  if(!spawn_data->spawn_is_async) {
    generic_spawn(io_child, MAM_I_NOT_STARTED);
    local_state = get_spawn_state(spawn_data->spawn_is_async);
    if (local_state == MAM_I_SPAWN_COMPLETED)
      deallocate_spawn_data();

  } else {
    local_state = spawn_data->spawn_is_single ? 
	    MAM_I_SPAWN_SINGLE_PENDING : MAM_I_SPAWN_PENDING;
    local_state = mall_conf->spawn_method == MAM_SPAWN_MERGE && spawn_data->initial_qty > spawn_data->target_qty ?
	    MAM_I_SPAWN_ADAPT_POSTPONE : local_state;
    set_spawn_state(local_state, 0);
    if((spawn_data->spawn_is_single && mall->myId == mall->root) || !spawn_data->spawn_is_single) {
      allocate_thread_spawn(io_child);
    }
  }
    
  return local_state;
}

/**
 * @brief Check/advance an in-progress spawn requested via ::init_spawn (sources side).
 *
 * For an asynchronous spawn, polls (or, if @p i_wait_completed, blocks until)
 * the auxiliary thread's progress and returns the resulting communicator once
 * ready. For a synchronous Merge shrink, performs the pending zombie split now
 * that data redistribution has completed.
 *
 * @param[in,out] io_child         Receives the resulting communicator to the
 *                                  new group of processes once the spawn
 *                                  completes.
 * @param[in]     i_comm           Communicator among the sources, used to
 *                                  synchronise/broadcast the spawn state.
 * @param[in]     i_wait_completed Non-zero (@c MAM_WAIT_COMPLETION) to block
 *                                  until completion; zero
 *                                  (@c MAM_CHECK_COMPLETION) to just poll the
 *                                  current state.
 * @return Spawn state (@c mam_inner_states); @c MAM_I_SPAWN_COMPLETED or
 *         @c MAM_I_SPAWN_ADAPTED once finished.
 */
int check_spawn_state(MPI_Comm *io_child, MPI_Comm i_comm, int i_wait_completed) { 
  int local_state;
  int global_state=MAM_I_NOT_STARTED;

  if(spawn_data->spawn_is_async) { // Async
    local_state = get_spawn_state(spawn_data->spawn_is_async);

    if(local_state == MAM_I_SPAWN_SINGLE_PENDING || local_state == MAM_I_SPAWN_SINGLE_COMPLETED) { // Single
      global_state = check_single_state(i_comm, io_child, local_state, i_wait_completed);

    } else if(local_state == MAM_I_SPAWN_PENDING || local_state == MAM_I_SPAWN_COMPLETED || local_state == MAM_I_SPAWN_ADAPTED) { // Generic
      global_state = check_generic_state(i_comm, local_state, i_wait_completed);

    } else if(local_state == MAM_I_SPAWN_ADAPT_POSTPONE) {
      global_state = local_state;
      
    } else {
      printf("Error Check spawn: Configuracion invalida State = %d\n", local_state);
      MPI_Abort(MPI_COMM_WORLD, -1);
      return -10;
    }
  } else if(mall_conf->spawn_method == MAM_SPAWN_MERGE){ // Start Merge shrink Sync
    generic_spawn(io_child, MAM_I_DIST_COMPLETED);
    global_state = get_spawn_state(spawn_data->spawn_is_async);
  }
  if(global_state == MAM_I_SPAWN_COMPLETED || global_state == MAM_I_SPAWN_ADAPTED)
    deallocate_spawn_data();

  return global_state;
}

/**
 * @brief Clear the @c MAM_I_SPAWN_ADAPT_POSTPONE flag blocking the auxiliary
 *        spawn threads under Merge shrink.
 *
 * The auxiliary threads are blocked on this flag so that the Merge shrink
 * (zombie split) does not proceed until data redistribution has completed.
 * Clearing it lets the threads continue.
 *
 * As a safety measure, the change is only applied if all 3 conditions hold:
 * the current internal spawn state is @c MAM_I_SPAWN_ADAPT_POSTPONE,
 * @p i_outside_state is @c MAM_I_SPAWN_ADAPT_PENDING, and the spawn is
 * asynchronous.
 *
 * @param[in] i_outside_state Spawn state as seen by the MaM state machine
 *                              outside of this module.
 */
void unset_spawn_postpone_flag(int i_outside_state) {
  int local_state = get_spawn_state(spawn_data->spawn_is_async);
  if(local_state == MAM_I_SPAWN_ADAPT_POSTPONE && i_outside_state == MAM_I_SPAWN_ADAPT_PENDING && spawn_data->spawn_is_async) { 
    set_spawn_state(MAM_I_SPAWN_PENDING, spawn_data->spawn_is_async);
    wakeup_redistribution();
  }
}

/**
 * @brief Blocking entry point for newly spawned children to join the reconfiguration.
 *
 * Builds a minimal spawn configuration and runs the children-side path of the
 * configured spawn method (Baseline or Merge), ensuring all spawn-creation
 * tasks complete correctly before returning. Children also obtain basic
 * information about the sources (process counts and root id) needed for the
 * later data-redistribution step.
 *
 * @param[in,out] io_parents Intercommunicator to the sources
 *                             (@c MPI_Comm_get_parent result) on entry;
 *                             under Merge, replaced by the merged intracomm
 *                             of targets on exit.
 */
void malleability_connect_children(MPI_Comm *io_parents) {
  size_t i;
  spawn_data = (Spawn_data *) malloc(sizeof(Spawn_data));

  spawn_data->initial_qty = mall->num_parents;
  spawn_data->target_qty = 0;
  for(i=0; i<mall->num_nodes; i++) { spawn_data->target_qty += mall->max_cpus[i] - mall->assigned_cpus[i]; }

  MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_SINGLE, &(spawn_data->spawn_is_single));
  MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_PTHREAD, &(spawn_data->spawn_is_async));
  MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_INTERCOMM, &(spawn_data->spawn_is_intercomm));
  MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_MULTIPLE, &(spawn_data->spawn_is_multiple));
  MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_PARALLEL, &(spawn_data->spawn_is_parallel));

  switch(mall_conf->spawn_method) {
    case MAM_SPAWN_BASELINE:
      spawn_data->spawn_qty = spawn_data->target_qty;
      baseline(*spawn_data, io_parents);
      if(!spawn_data->spawn_is_intercomm) {
        intracomm_strategy(MAM_TARGETS, io_parents);
      }
      break;
    case MAM_SPAWN_MERGE:
      spawn_data->spawn_qty = spawn_data->target_qty - spawn_data->initial_qty;
      merge(*spawn_data, io_parents, MAM_I_NOT_STARTED);
      break;
  }
  free(spawn_data);
}

//--------------PRIVATE CONFIGURATION FUNCTIONS---------------//

/**
 * @brief Build the global ::spawn_data configuration for a sources-side spawn.
 *
 * Allocates and fills the singleton ::spawn_data used by the rest of this
 * module. In particular:
 * - @c spawn_is_single / @c spawn_is_async / @c spawn_is_intercomm /
 *   @c spawn_is_multiple / @c spawn_is_parallel are read from
 *   @c MAM_SPAWN_STRATEGIES (@c mam_spawn_strategies) and enable the Single,
 *   threaded-async, Intercomm, Multiple and Parallel strategies respectively.
 * - @c mapping_fill_method is fixed to @c MAM_PHY_TYPE_HOSTFILE (hostfile-based
 *   physical mapping); @c MAM_PHY_TYPE_STRING is not used here.
 * - Under Baseline, @c spawn_qty equals @c target_qty and @c already_created
 *   is @c 0: every target process is newly spawned (no sources are reused).
 * - Under Merge, @c spawn_qty is @c target_qty @c - @c initial_qty (only the
 *   growth is spawned) and @c already_created is @c initial_qty: the sources
 *   are reused as targets and only the difference in process count needs to
 *   be created.
 *
 * If the spawn is asynchronous, also initialises the spawn-state
 * synchronisation primitives (::init_spawn_state).
 *
 * @param[in] i_comm Communicator among the sources requesting the spawn.
 */
void set_spawn_configuration(MPI_Comm i_comm) {
  size_t i;
  spawn_data = (Spawn_data *) malloc(sizeof(Spawn_data));

  spawn_data->total_spawns = 0;
  spawn_data->initial_qty = mall->numP;
  spawn_data->target_qty = 0;
  for(i=0; i<mall->num_nodes; i++) { spawn_data->target_qty += mall->max_cpus[i] - mall->assigned_cpus[i]; }

  MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_SINGLE, &(spawn_data->spawn_is_single)); 
  MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_PTHREAD, &(spawn_data->spawn_is_async));
  MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_INTERCOMM, &(spawn_data->spawn_is_intercomm));
  MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_MULTIPLE, &(spawn_data->spawn_is_multiple));
  MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_PARALLEL, &(spawn_data->spawn_is_parallel));
  spawn_data->comm = i_comm;
  spawn_data->mapping_fill_method = MAM_PHY_TYPE_HOSTFILE;
  spawn_data->sets = NULL;

  switch(mall_conf->spawn_method) {
    case MAM_SPAWN_BASELINE:
      spawn_data->spawn_qty = spawn_data->target_qty;
      spawn_data->already_created = 0;
      break;
    case MAM_SPAWN_MERGE:
      spawn_data->spawn_qty = spawn_data->target_qty - spawn_data->initial_qty;
      spawn_data->already_created = spawn_data->initial_qty;
      break;
  }

  if(spawn_data->spawn_is_async) {
    init_spawn_state();
  }
}

/**
 * @brief Free the global ::spawn_data structure and everything it owns.
 *
 * Releases the @c MPI_Info mapping of every spawn set, the @c sets array
 * itself, and, if the spawn was asynchronous, the spawn-state synchronisation
 * primitives (::free_spawn_state). Safe to call when ::spawn_data is already
 * @c NULL.
 */
void deallocate_spawn_data(void) {
  int i;
  MPI_Info *info;
  if(spawn_data == NULL) return;

  for(i=0; i<spawn_data->total_spawns; i++) {
    info = &(spawn_data->sets[i].mapping);
    if(*info != MPI_INFO_NULL) {
      MPI_Info_free(info);
      *info = MPI_INFO_NULL;
    }
  }

  if(spawn_data->sets != NULL) {
    free(spawn_data->sets);
    spawn_data->sets = NULL;
  }

  if(spawn_data->spawn_is_async) {
    free_spawn_state();
  }
  free(spawn_data); 
  spawn_data = NULL;
}


//--------------PRIVATE SPAWN CREATION FUNCTIONS---------------//

/**
 * @brief Generic process-creation step, dispatching to Baseline or Merge.
 *
 * When there are processes left to spawn (@c spawn_data->spawn_qty @c > @c 0),
 * computes and broadcasts the physical distribution of the new children
 * (::processes_dist) before creating them. When @p i_data_stage is
 * @c MAM_I_DIST_COMPLETED, also frees the logical occupancy of the ranks
 * being removed (::remove_dist), used by the Merge shrink path once data
 * redistribution has finished.
 *
 * Runs the configured spawn method (Baseline or Merge) and, on completion,
 * updates the global spawn state (::set_spawn_state), except when the state
 * read back after running the method is @c MAM_I_SPAWN_PENDING while the
 * value just computed is @c MAM_I_SPAWN_ADAPT_POSTPONE, in which case the
 * update is skipped to avoid overwriting a more advanced state.
 *
 * @param[in,out] io_child     Receives the resulting communicator to the new
 *                               group of processes (see ::baseline / ::merge).
 * @param[in]     i_data_stage Data-redistribution stage (@c mam_inner_states):
 *                               @c MAM_I_NOT_STARTED for the initial spawn
 *                               attempt, @c MAM_I_DIST_COMPLETED once data
 *                               redistribution has completed (Merge shrink).
 */
void generic_spawn(MPI_Comm *io_child, int i_data_stage) {
  int local_state = MAM_I_UNRESERVED;
  int aux_state;

  // WORK
  if(i_data_stage == MAM_I_DIST_COMPLETED) { //REMOVE FROM CONFIG UNNEDEED RANKS
    remove_dist(*spawn_data);
  }

  if(spawn_data->spawn_qty > 0) { //SET MAPPING FOR NEW PROCESSES
    if(mall->myId == mall->root) processes_dist(spawn_data);
    MPI_Bcast(mall->spawned_cpus, mall->num_nodes, MPI_INT, MAM_ROOT, spawn_data->comm);
  }

  switch(mall_conf->spawn_method) {
    case MAM_SPAWN_BASELINE:
      local_state = baseline(*spawn_data, io_child);
      if(!spawn_data->spawn_is_intercomm) {
        local_state = intracomm_strategy(MAM_SOURCES, io_child);
      }
      break;
    case MAM_SPAWN_MERGE:
      local_state = merge(*spawn_data, io_child, i_data_stage);
      break;
  }
  // END WORK
  aux_state = get_spawn_state(spawn_data->spawn_is_async);
  if(!(aux_state == MAM_I_SPAWN_PENDING && local_state == MAM_I_SPAWN_ADAPT_POSTPONE)) {
    set_spawn_state(local_state, spawn_data->spawn_is_async);
  }
}


//--------------PRIVATE THREAD FUNCTIONS---------------//

/**
 * @brief Create and detach the auxiliary thread that performs asynchronous spawn work.
 *
 * The thread cannot be joined; it detaches itself immediately and releases
 * its own resources once ::thread_work finishes.
 *
 * @param[in,out] io_child Communicator pointer forwarded to ::thread_work /
 *                          ::generic_spawn; receives the resulting
 *                          communicator once the thread completes its work.
 * @return @c 0 on success; @c -1 if thread creation or detaching failed (the
 *         MPI job is aborted before returning in that case).
 */
int allocate_thread_spawn(MPI_Comm *io_child) {
  if(pthread_create(&spawn_thread, NULL, thread_work, (void *) io_child)) {
    printf("Error al crear el hilo de SPAWN\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    return -1;
  }
  if(pthread_detach(spawn_thread)) {
    printf("Error when detaching spawning thread\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    return -1;
  }
  return 0;
}

/**
 * @brief Auxiliary-thread entry point that performs the spawn work in the background.
 *
 * Runs ::generic_spawn to configure/create the new group of processes. If the
 * result is @c MAM_I_SPAWN_ADAPT_POSTPONE or @c MAM_I_SPAWN_PENDING (Merge
 * shrink awaiting data redistribution), blocks on ::wait_redistribution and
 * then finishes the process-creation step with a second ::generic_spawn call
 * (@c MAM_I_DIST_COMPLETED). Finally wakes up the master thread via
 * ::wakeup_completion and terminates.
 *
 * @param[in,out] io_args Communicator pointer (@c MPI_Comm @c *), passed as
 *                          @c void @c * per the pthread API; forwarded to
 *                          ::generic_spawn to receive the resulting
 *                          communicator.
 * @return Always @c NULL (the thread exits via @c pthread_exit).
 */
void* thread_work(void *io_args) {
  int local_state;
  MPI_Comm *child = (MPI_Comm *) io_args;
 
  generic_spawn(child, MAM_I_NOT_STARTED);

  local_state = get_spawn_state(spawn_data->spawn_is_async);
  if(local_state == MAM_I_SPAWN_ADAPT_POSTPONE || local_state == MAM_I_SPAWN_PENDING) {
    // The group of processes will finish joining once data redistribution completes

    local_state = wait_redistribution();
    generic_spawn(child, MAM_I_DIST_COMPLETED);
  }
  wakeup_completion();

  pthread_exit(NULL);
}

/**
 * @brief Check whether the asynchronous Single-strategy spawn step has finished.
 *
 * On the root process, optionally blocks (@p i_wait_completed) until the
 * auxiliary thread reports completion; the resulting state is then broadcast
 * to the rest of the sources. Non-root processes must join here regardless,
 * both to finalise the spawn and if the application has otherwise ended its
 * work.
 *
 * If the step has completed (@c MAM_I_SPAWN_SINGLE_COMPLETED), the state is
 * advanced to @c MAM_I_SPAWN_PENDING and non-root processes create their own
 * auxiliary thread (::allocate_thread_spawn) to continue with the generic
 * spawn step. Otherwise @c MAM_I_SPAWN_SINGLE_PENDING is kept.
 *
 * @param[in]     i_comm           Communicator among the sources, used to
 *                                  broadcast the resulting state from root.
 * @param[in,out] io_child         Forwarded to ::allocate_thread_spawn for
 *                                  non-root processes once the step completes.
 * @param[in]     i_global_state   Current known state
 *                                  (@c MAM_I_SPAWN_SINGLE_PENDING or
 *                                  @c MAM_I_SPAWN_SINGLE_COMPLETED).
 * @param[in]     i_wait_completed Non-zero to block (root only) until the
 *                                  auxiliary thread reports completion.
 * @return Resulting state: @c MAM_I_SPAWN_SINGLE_PENDING if still in
 *         progress, or @c MAM_I_SPAWN_PENDING once the Single step has
 *         completed.
 */
int check_single_state(MPI_Comm i_comm, MPI_Comm *io_child, int i_global_state, int i_wait_completed) {
  while(i_wait_completed && mall->myId == mall->root && i_global_state == MAM_I_SPAWN_SINGLE_PENDING) {
    i_global_state = wait_completion();
  }
  MPI_Bcast(&i_global_state, 1, MPI_INT, mall->root, i_comm);

  // Non-root processes join root to finalize the spawn
  // They also must join if the application has ended its work
  if(i_global_state == MAM_I_SPAWN_SINGLE_COMPLETED) { 
    i_global_state = MAM_I_SPAWN_PENDING;
    set_spawn_state(i_global_state, spawn_data->spawn_is_async);

    if(mall->myId != mall->root) {
      allocate_thread_spawn(io_child);
    }
  }
  return i_global_state;
}

/**
 * @brief Check whether the asynchronous generic spawn step has finished for all sources.
 *
 * Optionally blocks (@p i_wait_completed) on this rank's auxiliary thread,
 * then reduces the local state across all sources with @c MPI_MIN so that the
 * step is only reported as finished once every source's thread has completed.
 * If the combined state is @c MAM_I_SPAWN_COMPLETED or @c MAM_I_SPAWN_ADAPTED,
 * also updates the global spawn state accordingly.
 *
 * @param[in] i_comm           Communicator among the sources, used for the
 *                              @c MPI_Allreduce state reduction.
 * @param[in] i_local_state    Current known local state for this rank
 *                              (@c MAM_I_SPAWN_PENDING, @c MAM_I_SPAWN_COMPLETED
 *                              or @c MAM_I_SPAWN_ADAPTED).
 * @param[in] i_wait_completed Non-zero to block until the local auxiliary
 *                              thread reports completion before reducing.
 * @return Combined state across all sources: @c MAM_I_SPAWN_PENDING if any
 *         source is still in progress, otherwise @c MAM_I_SPAWN_COMPLETED or
 *         @c MAM_I_SPAWN_ADAPTED.
 */
int check_generic_state(MPI_Comm i_comm, int i_local_state, int i_wait_completed) {
  int global_state;

  while(i_wait_completed && i_local_state == MAM_I_SPAWN_PENDING) i_local_state = wait_completion();

  MPI_Allreduce(&i_local_state, &global_state, 1, MPI_INT, MPI_MIN, i_comm);
  if(global_state == MAM_I_SPAWN_COMPLETED || global_state == MAM_I_SPAWN_ADAPTED) {
    set_spawn_state(global_state, spawn_data->spawn_is_async);
  }
  return global_state;
}
