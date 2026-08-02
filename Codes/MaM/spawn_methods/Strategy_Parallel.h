#ifndef MAM_SPAWN_PARALLEL_H
#define MAM_SPAWN_PARALLEL_H

/**
 * @file Strategy_Parallel.h
 * @brief Parallel spawn strategy: cascading/recursive spawn tree, then binary-tree merge.
 *
 * Unlike a single collective @c MPI_Comm_spawn call, the sources -- and later
 * every group of children they create -- each spawn only a small subgroup of
 * further children, cascading recursively until the whole requested amount
 * of processes exists. Depending on whether the requested node distribution
 * is homogeneous (::check_homogenous_dist), either the Hypercube or the
 * Iterative Diffusive algorithm is used to decide who spawns what. The
 * overall flow is:
 * -# Cascading spawn: Hypercube (homogeneous) or Iterative Diffusive
 *    (heterogeneous) group sizes.
 * -# ::common_synch: an upside (children-to-root) then downside
 *    (root-to-children) token relay so every spawned group knows the whole
 *    tree has finished spawning before continuing.
 * -# Sources (parents): disconnect the temporary spawn-tree communicators
 *    and @c MPI_Comm_accept the final connection from the merged children.
 * -# Children: merge every spawned group into a single intracommunicator via
 *    a binary tree of MPI ports (::binary_tree_connection), reorder ranks to
 *    their expected logical id (::binary_tree_reorder), discover the
 *    sources' port and @c MPI_Comm_connect back to them.
 *
 * Terminology (see Spawn_DataStructure.h): sources are the ranks before the
 * reconfiguration (also parents); children are the newly spawned ranks
 * (always targets); targets are the ranks that continue afterwards
 * (children, plus reused sources under Merge).
 */

#include <mpi.h>
#include "Spawn_DataStructure.h"

/**
 * @brief Parents (sources) side of Parallel: cascading spawn, synchronise, then accept children.
 *
 * Broadcasts @c total_spawns to the sources, opens the top-level port that
 * the merged children will eventually connect to, then dispatches to either
 * ::hypercube_spawn or ::diffusive_iterative_spawn depending on
 * ::check_homogenous_dist to cascade-spawn the first level of children.
 * Runs ::common_synch (as the root of the recursion, with no outer parent)
 * to wait until every spawned subtree has finished spawning, disconnects the
 * temporary spawn-tree communicators, and finally @c MPI_Comm_accept's the
 * connection from the fully merged group of children.
 *
 * @param[in]     i_spawn_data Spawn configuration (@c total_spawns,
 *                               @c initial_qty, @c comm).
 * @param[in,out] io_spawn_port Ports structure used to open the top-level
 *                               port and later accept the children.
 * @param[out]    o_child      Receives the parent-children intercommunicator
 *                               once the merged children connect back.
 */
void parallel_strat_parents(Spawn_data i_spawn_data, Spawn_ports *io_spawn_port, MPI_Comm *o_child);

/**
 * @brief Children side of Parallel: continue the cascading spawn, synchronise,
 *        merge with sibling groups, and reconnect to the sources.
 *
 * Recomputes this group's id/group count from @c mall, opens this group's
 * port if it belongs to the lower half of the binary tree, and -- depending
 * on ::check_homogenous_dist -- possibly cascades further spawns of its own
 * (::hypercube_spawn / ::diffusive_iterative_spawn) before running
 * ::common_synch against its own immediate parent group. After
 * disconnecting the temporary spawn-tree and parent communicators, merges
 * with every sibling group into one intracommunicator
 * (::binary_tree_connection), reorders ranks to their expected logical id
 * (::binary_tree_reorder), discovers the sources' published port and
 * @c MPI_Comm_connect's back to them, then updates MaM's communicators to
 * the newly merged group.
 *
 * @param[in]     i_spawn_data Spawn configuration (@c initial_qty).
 * @param[in,out] io_spawn_port Ports structure used for this group's own
 *                               port and for discovering sibling/parent ports.
 * @param[in,out] io_parents   Intercommunicator to this group's immediate
 *                               spawning parent on entry; replaced by the
 *                               new intercommunicator to the sources on exit.
 */
void parallel_strat_children(Spawn_data i_spawn_data, Spawn_ports *io_spawn_port, MPI_Comm *io_parents);

#endif
