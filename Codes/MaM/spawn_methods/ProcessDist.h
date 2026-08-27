#ifndef MAM_SPAWN_PROCESS_DIST_H
#define MAM_SPAWN_PROCESS_DIST_H

/**
 * @file ProcessDist.h
 * @brief MaM physical distribution of newly spawned children across nodes,
 *        hostfile generation, and logical cleanup of removed ranks.
 */

#include "Spawn_DataStructure.h"

/**
 * @brief Compute the physical distribution of children to spawn and build the
 *        MPI_Info mapping used by MPI_Comm_spawn.
 *
 * Determines how many nodes/cores each new spawn set will use, allocates the
 * array of spawn sets, and fills each set's command and host/hostfile
 * MPI_Info describing where the children will be created.
 *
 * @param[in,out] io_spawn_data Spawn configuration; @c total_spawns and
 *                               @c sets are computed/allocated here.
 */
void processes_dist(Spawn_data *io_spawn_data);

/**
 * @brief Check whether all currently used nodes host the same number of
 *        spawned processes.
 *
 * @return Non-zero if the distribution is homogeneous (or no node is used),
 *         0 otherwise.
 */
extern int check_homogenous_dist(void);

/**
 * @brief Mark the ranks to be removed as free space in MaM's logical
 *        per-node occupancy state.
 *
 * @param[in] i_spawn_data Spawn configuration; @c initial_qty and
 *                          @c target_qty give the number of ranks to free.
 *
 * @note FIXME: Assumes the library can freely choose which ranks/nodes
 *       should be returned.
 * @note TODO: Should consider removing full nodes if possible, with
 *       preference to Intercomm nodes.
 */
extern void remove_dist(Spawn_data i_spawn_data);

/**
 * @brief Build (or extend) the hostfile name for a given job id and spawn
 *        index.
 *
 * On the first call for a given file (@p io_n == 0) allocates and writes the
 * full name; subsequent calls only patch the numeric index suffix.
 *
 * @param[in,out] io_file_name Pointer to the hostfile name buffer; allocated
 *                              on first use.
 * @param[in,out] io_n         Pointer to a flag; 0 on first call, set to 1
 *                              afterwards.
 * @param[in]     i_jid        Job id string used to build the file name.
 * @param[in]     i_index      Spawn index used to build the numeric suffix
 *                              of the file name.
 */
void set_hostfile_name(char **io_file_name, int *io_n, const char *i_jid, int i_index);

/**
 * @brief Read a hostfile and accumulate the total number of processes
 *        assigned across all of its lines.
 *
 * @param[in]  i_file_name Path of the hostfile to read.
 * @param[out] o_qty       Receives the total process count read from the
 *                          file.
 * @return Always 0.
 */
int read_hostfile_procs(char *i_file_name, int *o_qty);

#endif
