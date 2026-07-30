#ifndef PROCESS_PHASE_H
#define PROCESS_PHASE_H

/**
 * @file process_phase.h
 * @brief Phase-level iteration drivers for the SAM synthetic application.
 */

#include "Main_datatypes.h"
#include "results.h"

/**
 * @brief Initialise all stages of every phase for the current process group.
 *
 * Calls ::init_stage for each stage. When @p i_compute is 0, only memory
 * setup is performed and any time spent is added to @c results->wasted_time.
 * When @p i_compute is 1, calibration work is performed to recompute
 * per-operation timings from scratch. All ranks must pass the same
 * @p i_compute value.
 *
 * @param[in,out] io_group       Current process-group state.
 * @param[in]     i_config_file  Loaded Proteo configuration.
 * @param[in,out] io_results     Results structure (may accrue wasted time).
 * @param[in]     i_compute      Non-zero to recalibrate stage timings.
 * @param[in]     i_comm         MPI communicator.
 */
void init_phases(group_data *io_group, configuration *i_config_file, results_data *io_results,
                 int i_compute, MPI_Comm i_comm);

/**
 * @brief Run emulated iterations until the group budget or all phases end.
 *
 * @retval 0 This process group's iteration budget is exhausted; the caller
 *           should start a malleability reconfiguration.
 * @retval 1 All phases have finished (end of the application).
 *
 * @param[in,out] io_group       Current process-group state.
 * @param[in]     i_config_file  Loaded Proteo configuration.
 * @param[in,out] io_results     Timing results to update.
 * @param[in]     i_comm         MPI communicator.
 */
int phase_normal(group_data *io_group, configuration *i_config_file, results_data *io_results,
                 MPI_Comm i_comm);

/**
 * @brief Run emulated iterations while a MaM reconfiguration runs in the background.
 *
 * Polls MaM via @c MAM_Checkpoint after each iteration. Today the return value
 * is unused by Main (follow-up work is the same either way); it is documented
 * for possible future use.
 *
 * @retval 0 MaM reached @c MAM_COMPLETED (including early exit).
 * @retval 1 All phases finished (waiting for MaM completion if still pending).
 *
 * @param[in,out] io_group       Current process-group state.
 * @param[in]     i_config_file  Loaded Proteo configuration.
 * @param[in,out] io_results     Timing results to update.
 * @param[in]     i_callback     User redistribution callback passed to MaM.
 * @param[in]     i_comm         MPI communicator.
 */
int phase_reconf(group_data *io_group, configuration *i_config_file, results_data *io_results,
                 void (*i_callback)(void *), MPI_Comm i_comm);

#endif
