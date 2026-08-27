#ifndef MAM_SPAWN_MERGE_H
#define MAM_SPAWN_MERGE_H

/**
 * @file Merge.h
 * @brief Merge spawn method: reuse sources as targets when expanding; zombie-split when shrinking.
 *
 * Under Merge expand, sources that continue are targets without being children;
 * only newly spawned ranks are children (and also targets).
 */

#include <mpi.h>
#include "Spawn_DataStructure.h"

/**
 * @brief Perform Merge expand (spawn + intracomm merge) or shrink (zombie split).
 *
 * @param[in]     i_spawn_data Spawn configuration (@c initial_qty / @c target_qty).
 * @param[in,out] io_child     Expand: intercomm then merged intracomm among targets.
 *                             Shrink: receives the split communicator for surviving ranks.
 * @param[in]     i_data_state Data-redistribution state (shrink waits for @c MAM_I_DIST_COMPLETED).
 * @return Spawn state (@c MAM_I_SPAWN_COMPLETED, @c MAM_I_SPAWN_ADAPTED, or @c MAM_I_SPAWN_ADAPT_POSTPONE).
 */
int merge(Spawn_data i_spawn_data, MPI_Comm *io_child, int i_data_state);

/**
 * @brief Merge a parent–children intercommunicator into one intracomm among targets.
 *
 * @param[in]     i_is_children_group Non-zero if this rank is a child (high group in merge).
 * @param[in,out] io_child            Intercomm on entry; intracomm of targets on exit.
 * @return @c MAM_I_SPAWN_COMPLETED.
 */
int intracomm_strategy(int i_is_children_group, MPI_Comm *io_child);

#endif
