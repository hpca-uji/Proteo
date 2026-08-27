#ifndef MAM_SPAWN_SINGLE_H
#define MAM_SPAWN_SINGLE_H

/**
 * @file Strategy_Single.h
 * @brief Single-root spawn strategy: one parent opens/connects; others join via port.
 */

#include <mpi.h>
#include "Spawn_DataStructure.h"

/**
 * @brief Parents side of Single: root receives children's port; all sources connect.
 *
 * @param[in]     i_spawn_data Spawn configuration (async flag for state updates).
 * @param[in,out] io_child     Root: spawn intercomm used to recv the port, then replaced
 *                             by the new parent–children intercomm. Non-root: receives
 *                             the connected intercomm.
 */
void single_strat_parents(Spawn_data i_spawn_data, MPI_Comm *io_child);

/**
 * @brief Children side of Single: root opens a port, sends it to parents, all accept.
 *
 * @param[in,out] io_parents    Parents intercomm; replaced by the new intercomm after accept.
 * @param[in,out] io_spawn_port Ports structure used to open the children's port.
 */
void single_strat_children(MPI_Comm *io_parents, Spawn_ports *io_spawn_port);

#endif
