#ifndef MAM_MANAGER_H
#define MAM_MANAGER_H

/**
 * @file MAM_Manager.h
 * @brief Public MaM lifecycle, checkpoint, and data-registration API.
 */

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mpi.h>
#include "MAM_Constants.h"

/**
 * @brief Initialise MaM on sources, or complete children join if spawned dynamically.
 *
 * @param[in]     i_root          Root rank among sources.
 * @param[in,out] io_comm         Application communicator (duplicated internally;
 *                                updated in place across reconfigurations).
 * @param[in]     i_name_exec     Executable path/name used for spawn.
 * @param[in]     i_user_function Optional callback invoked during the user phase
 *                                (may be @c NULL on sources that only spawn).
 * @param[in]     i_user_args     Opaque argument passed to @p i_user_function.
 * @return @c MAM_SOURCES for the initial parent group, @c MAM_TARGETS for children.
 */
int MAM_Init(int i_root, MPI_Comm *io_comm, char *i_name_exec,
             void (*i_user_function)(void *), void *i_user_args);

/**
 * @brief Tear down MaM, free registries, and wake any Merge zombies.
 * @return @c MAM_OK, or non-zero if zombie awake requests abort.
 */
int MAM_Finalize(void);

/**
 * @brief Drive one step (or wait) of the reconfiguration state machine.
 *
 * @param[out]    o_mam_state      Receives public ::mam_states progress.
 * @param[in]     i_wait_completed @c MAM_WAIT_COMPLETION or @c MAM_CHECK_COMPLETION.
 * @param[in]     i_user_function  User callback for the user-pending phase.
 * @param[in]     i_user_args      Argument for @p i_user_function.
 * @return @c MAM_OK or @c MAM_DENIED.
 */
int MAM_Checkpoint(int *o_mam_state, int i_wait_completed,
                   void (*i_user_function)(void *), void *i_user_args);

/**
 * @brief Resume redistribution after a postponed Merge shrink adaptation.
 * @param[out] o_mam_state Receives updated public state.
 */
void MAM_Resume_redistribution(int *o_mam_state);


/**
 * @brief Copy the latest ::mam_user_reconf_t snapshot for the application.
 * @param[out] o_reconf_info Filled with numS/numT/rank_state/comm.
 * @return @c MAM_OK.
 */
int MAM_Get_Reconf_Info(mam_user_reconf_t *o_reconf_info);

/**
 * @brief Register a data array in one of the four registries.
 *
 * @param[in]  i_data          Local buffer (caller-owned).
 * @param[out] o_index         Receives the new entry index.
 * @param[in]  i_total_qty     Global element count.
 * @param[in]  i_type          Element MPI datatype.
 * @param[in]  i_is_replicated @c MAM_DATA_REPLICATED or @c MAM_DATA_DISTRIBUTED.
 * @param[in]  i_is_constant   @c MAM_DATA_CONSTANT or @c MAM_DATA_VARIABLE.
 */
void MAM_Data_add(void *i_data, size_t *o_index, size_t i_total_qty, MPI_Datatype i_type,
                  int i_is_replicated, int i_is_constant);

/**
 * @brief Replace a previously registered entry.
 *
 * @param[in] i_data          New local buffer (caller-owned).
 * @param[in] i_index         Entry index.
 * @param[in] i_total_qty     Global element count.
 * @param[in] i_type          Element MPI datatype.
 * @param[in] i_is_replicated Registry selector.
 * @param[in] i_is_constant   Registry selector.
 */
void MAM_Data_modify(void *i_data, size_t i_index, size_t i_total_qty, MPI_Datatype i_type,
                     int i_is_replicated, int i_is_constant);

/**
 * @brief Query how many entries are in a registry.
 *
 * @param[in]  i_is_replicated Registry selector.
 * @param[in]  i_is_constant   Registry selector.
 * @param[out] o_entries       Receives entry count.
 */
void MAM_Data_get_entries(int i_is_replicated, int i_is_constant, size_t *o_entries);

/**
 * @brief Retrieve a registered entry's buffer pointer and metadata.
 *
 * @param[out] o_data          Receives the local buffer pointer.
 * @param[in]  i_index         Entry index.
 * @param[out] o_total_qty     Receives global element count.
 * @param[out] o_type          Receives MPI datatype.
 * @param[in]  i_is_replicated Registry selector.
 * @param[in]  i_is_constant   Registry selector.
 */
void MAM_Data_get_pointer(void **o_data, size_t i_index, size_t *o_total_qty, MPI_Datatype *o_type,
                         int i_is_replicated, int i_is_constant);
#endif
