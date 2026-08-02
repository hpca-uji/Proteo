#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "MAM_Constants.h"
#include "MAM_DataStructures.h"
#include "Baseline.h"
#include "SpawnUtils.h"
#include "Strategy_Single.h"
#include "Strategy_Multiple.h"
#include "Strategy_Parallel.h"
#include "PortService.h"

/**
 * @file Baseline.c
 * @brief Implementation of the Baseline spawn method and strategy dispatch.
 */

void baseline_parents(Spawn_data i_spawn_data, Spawn_ports *io_spawn_port, MPI_Comm *o_child);
void baseline_children(Spawn_data i_spawn_data, Spawn_ports *io_spawn_port, MPI_Comm *io_parents);

/**
 * @brief Run the Baseline spawn path for sources (parents) or children.
 *
 * Sources (@c MPI_Comm_get_parent == @c MPI_COMM_NULL) run ::baseline_parents;
 * children run ::baseline_children. Parallel strategy handles its own full path.
 *
 * @param[in]     i_spawn_data Spawn configuration.
 * @param[in,out] io_child     Parents: receives intercomm to children.
 *                             Children: parents intercomm on entry.
 * @return @c MAM_I_SPAWN_COMPLETED.
 *
 * @note TODO: Error handling for failed spawns.
 * @note FIXME: @c MPI_Comm_get_parent may be wrong for a third or later
 *       reconfiguration that only expands.
 */
int baseline(Spawn_data i_spawn_data, MPI_Comm *io_child) {
  Spawn_ports spawn_port;
  MPI_Comm intercomm;
  MPI_Comm_get_parent(&intercomm); // FIXME: May be a problem for third reconf or more with only expansions
  init_ports(&spawn_port);

  if (intercomm == MPI_COMM_NULL) { // Parents (sources) path
    baseline_parents(i_spawn_data, &spawn_port, io_child);
  } else { // Children path
    baseline_children(i_spawn_data, &spawn_port, io_child);
  }

  free_ports(&spawn_port);
  return MAM_I_SPAWN_COMPLETED;
}

/**
 * @brief Sources/parents side of Baseline: spawn children and apply strategies.
 *
 * Parallel takes over entirely. Otherwise spawns each ::Spawn_set, then
 * optionally Multiple (merge spawn intercomms) and Single (port handoff).
 *
 * @param[in]     i_spawn_data  Spawn configuration.
 * @param[in,out] io_spawn_port Ports structure for strategies that need it.
 * @param[out]    o_child       Receives the final parent–children intercomm.
 *
 * @note TODO: Deactivate Multiple before spawning when @c total_spawns == 1.
 */
void baseline_parents(Spawn_data i_spawn_data, Spawn_ports *io_spawn_port, MPI_Comm *o_child) {
  int i;
  MPI_Comm comm, *intercomms;

  #if MAM_DEBUG >= 3
    DEBUG_FUNC("Starting spawning of processes", mall->myId, mall->numP); fflush(stdout);
  #endif

  if (i_spawn_data.spawn_is_parallel) {
    // Parallel handles the full spawn, sync, and reconnect path itself.
    parallel_strat_parents(i_spawn_data, io_spawn_port, o_child);
    return;
  }

  if (i_spawn_data.spawn_is_single && mall->myId != mall->root) {
    single_strat_parents(i_spawn_data, o_child);
    return;
  }

  comm = i_spawn_data.spawn_is_single ? MPI_COMM_SELF : i_spawn_data.comm;
  MPI_Bcast(&i_spawn_data.total_spawns, 1, MPI_INT, mall->root, comm);
  intercomms = (MPI_Comm *)malloc(i_spawn_data.total_spawns * sizeof(MPI_Comm));
  if (mall->myId != mall->root) {
    i_spawn_data.sets = (Spawn_set *)malloc(i_spawn_data.total_spawns * sizeof(Spawn_set));
  }

  for (i = 0; i < i_spawn_data.total_spawns; i++) {
    mam_spawn(i_spawn_data.sets[i], comm, &intercomms[i]);
  }
  #if MAM_DEBUG >= 3
    DEBUG_FUNC("Sources have created the new processes. Performing additional actions if required.", mall->myId, mall->numP); fflush(stdout);
  #endif

  // TODO: Deactivate Multiple spawn before spawning if total_spawns == 1
  if (i_spawn_data.spawn_is_multiple) { multiple_strat_parents(i_spawn_data, io_spawn_port, comm, intercomms, o_child); }
  else { *o_child = intercomms[0]; }

  if (i_spawn_data.spawn_is_single) { single_strat_parents(i_spawn_data, o_child); }

  free(intercomms);
  if (mall->myId != mall->root) { free(i_spawn_data.sets); }
}

/**
 * @brief Children side of Baseline: join Multiple/Single/Parallel post-spawn paths.
 *
 * @param[in]     i_spawn_data  Spawn configuration.
 * @param[in,out] io_spawn_port Ports structure for strategies that need it.
 * @param[in,out] io_parents    Parents intercommunicator (@c MPI_Comm_get_parent).
 */
void baseline_children(Spawn_data i_spawn_data, Spawn_ports *io_spawn_port, MPI_Comm *io_parents) {
  if (i_spawn_data.spawn_is_parallel) {
    // Parallel handles the full spawn, sync, merge, and reconnect path itself.
    parallel_strat_children(i_spawn_data, io_spawn_port, io_parents);
    return;
  }

  if (i_spawn_data.spawn_is_multiple) { multiple_strat_children(io_parents, io_spawn_port); }
  if (i_spawn_data.spawn_is_single) { single_strat_children(io_parents, io_spawn_port); }
}
