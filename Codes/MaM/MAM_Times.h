#ifndef MAM_TIMES_H
#define MAM_TIMES_H

/**
 * @file MAM_Times.h
 * @brief Internal timing buffer init/reset/free and broadcast helpers.
 */

#include <mpi.h>

/**
 * @brief Allocate ::mall_conf->times, reset fields, and commit the MPI datatype.
 */
void init_malleability_times(void);

/**
 * @brief Zero timing scalars before a reconfiguration.
 */
void reset_malleability_times(void);

/**
 * @brief Free the timing structure and its MPI datatype.
 */
void free_malleability_times(void);

/**
 * @brief Broadcast packed timing fields over @c mall->intercomm.
 *
 * Useful when Baseline sources will not survive and targets must retrieve times.
 *
 * @param[in] i_root Bcast root (@c MPI_ROOT / @c MPI_PROC_NULL on intercomm).
 */
void malleability_times_broadcast(int i_root);

#endif
