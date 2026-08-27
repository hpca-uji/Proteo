#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "MAM_Constants.h"
#include "MAM_DataStructures.h"
#include "Merge.h"
#include "Baseline.h"

/**
 * @file Merge.c
 * @brief Implementation of Merge expand/shrink adaptation.
 */

void merge_adapt_expand(MPI_Comm *io_child, int i_is_children_group);
void merge_adapt_shrink(int i_numC, MPI_Comm *io_child, MPI_Comm i_comm, int i_myId);

/**
 * @brief Perform Merge expand (spawn + intracomm merge) or shrink (zombie split).
 *
 * Shrink: if redistribution is done, split so high ranks become zombies
 * (@c MAM_I_SPAWN_ADAPTED); otherwise postpone (@c MAM_I_SPAWN_ADAPT_POSTPONE).
 * Expand: Baseline spawn then merge parents and children into one intracomm
 * of targets (@c MAM_I_SPAWN_COMPLETED).
 *
 * @param[in]     i_spawn_data Spawn configuration (@c initial_qty / @c target_qty).
 * @param[in,out] io_child     Expand/shrink communicator as described in the header.
 * @param[in]     i_data_state Data-redistribution state.
 * @return Spawn state for the MaM state machine.
 */
int merge(Spawn_data i_spawn_data, MPI_Comm *io_child, int i_data_state) {
  MPI_Comm intercomm;
  int local_state;
  int is_children_group = 1;

  if (i_spawn_data.initial_qty > i_spawn_data.target_qty) { // Shrink
    if (i_data_state == MAM_I_DIST_COMPLETED) {
      merge_adapt_shrink(i_spawn_data.target_qty, io_child, i_spawn_data.comm, mall->myId);
      local_state = MAM_I_SPAWN_ADAPTED;
    } else {
      local_state = MAM_I_SPAWN_ADAPT_POSTPONE;
    }
  } else { // Expand
    MPI_Comm_get_parent(&intercomm);
    is_children_group = intercomm == MPI_COMM_NULL ? 0 : 1;

    baseline(i_spawn_data, io_child);
    merge_adapt_expand(io_child, is_children_group);
    local_state = MAM_I_SPAWN_COMPLETED;
  }

  return local_state;
}

/**
 * @brief Merge a parent–children intercommunicator into one intracomm among targets.
 *
 * @param[in]     i_is_children_group Non-zero if this rank is a child (high group in merge).
 * @param[in,out] io_child            Intercomm on entry; intracomm of targets on exit.
 * @return @c MAM_I_SPAWN_COMPLETED.
 */
int intracomm_strategy(int i_is_children_group, MPI_Comm *io_child) {
  merge_adapt_expand(io_child, i_is_children_group);
  return MAM_I_SPAWN_COMPLETED;
}

/**
 * @brief Merge parents and children into a single intracomm of targets.
 *
 * Called before data redistribution. Sources that continue are targets that
 * are not children; newly spawned ranks are both children and targets.
 * @c MPI_Intercomm_merge uses @p i_is_children_group as the high-value flag
 * (ranks that pass 0 appear first in the new intracomm).
 *
 * @param[in,out] io_child            Intercomm on entry; intracomm on exit.
 * @param[in]     i_is_children_group Non-zero for the children group.
 *
 * @note TODO: REFACTOR.
 */
void merge_adapt_expand(MPI_Comm *io_child, int i_is_children_group) {
  MPI_Comm new_comm = MPI_COMM_NULL;

  MPI_Intercomm_merge(*io_child, i_is_children_group, &new_comm); // Ranks that pass 0 come first

  MPI_Comm_disconnect(io_child);
  *io_child = new_comm;
}


/**
 * @brief Split so that only the first @p i_numC ranks remain active targets.
 *
 * Ranks with id >= @p i_numC become zombies (undefined color in the split).
 * Called after data redistribution has completed.
 *
 * @param[in]     i_numC   Number of surviving target ranks.
 * @param[in,out] io_child Previous child/returned comm (disconnected if set); receives split result.
 * @param[in]     i_comm   Intracommunicator to split (sources' @c spawn_data.comm).
 * @param[in]     i_myId   Local rank in @p i_comm.
 */
void merge_adapt_shrink(int i_numC, MPI_Comm *io_child, MPI_Comm i_comm, int i_myId) {
  int color = MPI_UNDEFINED;

  if (*io_child != MPI_COMM_NULL && *io_child != MPI_COMM_WORLD) MPI_Comm_disconnect(io_child);
  if (i_myId < i_numC) {
      color = 1;
  }
  MPI_Comm_split(i_comm, color, i_myId, io_child);
}
