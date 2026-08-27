#ifndef MAM_SPAWN_MULTIPLE_H
#define MAM_SPAWN_MULTIPLE_H

/**
 * @file Strategy_Multiple.h
 * @brief Multiple strategy: one spawn per node, then merge children and reconnect to parents.
 *
 * Isolates each group's @c MPI_COMM_WORLD on a separate node so Termination Shrinkage
 * can fully release nodes. Spawns are sequential per source root; children merge via ports.
 */

#include <mpi.h>
#include "Spawn_DataStructure.h"

/**
 * @brief Parents side: coordinate child-group merge order, then connect to merged children.
 *
 * @param[in]     i_spawn_data  Spawn configuration (@c total_spawns).
 * @param[in,out] io_spawn_port Ports for discovering the children's published service.
 * @param[in]     i_comm        Intracommunicator among sources used for the final connect.
 * @param[in,out] io_intercomms Per-spawn intercomms from ::mam_spawn (disconnected here).
 * @param[out]    o_child       Receives the final parent–children intercommunicator.
 */
void multiple_strat_parents(Spawn_data i_spawn_data, Spawn_ports *io_spawn_port, MPI_Comm i_comm,
                            MPI_Comm *io_intercomms, MPI_Comm *o_child);

/**
 * @brief Children side: merge all child groups into one intracomm, then accept parents.
 *
 * @param[in,out] io_parents    Parents intercomm on entry; final intercomm on exit.
 * @param[in,out] io_spawn_port Ports for publish/lookup during the merge.
 */
void multiple_strat_children(MPI_Comm *io_parents, Spawn_ports *io_spawn_port);

#endif
