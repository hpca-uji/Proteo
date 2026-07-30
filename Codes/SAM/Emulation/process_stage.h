#ifndef PROCESS_STAGE_H
#define PROCESS_STAGE_H

/**
 * @file process_stage.h
 * @brief Stage initialisation and execution dispatch for SAM.
 */

#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include "Main_datatypes.h"

/**
 * @brief Stage procedure types (maps to JSON/INI @c Stage_Type).
 *
 * Values: 0-1 compute, 2-8 communication, 9-10 I/O.
 */
enum compute_methods {
  COMP_PI = 0,       /**< Serial π compute kernel. */
  COMP_MATRIX,       /**< Synthetic matrix CPU load. */
  COMP_POINT,        /**< Blocking pairwise P2P. */
  COMP_IPOINT,       /**< Non-blocking pairwise P2P. */
  COMP_WAIT,         /**< Wait/cancel pending non-blocking requests. */
  COMP_BCAST,        /**< MPI broadcast. */
  COMP_ALLGATHER,    /**< MPI Allgatherv. */
  COMP_REDUCE,       /**< MPI Reduce. */
  COMP_ALLREDUCE,    /**< MPI Allreduce. */
  COMP_IOWRITE,      /**< File write stage. */
  COMP_IOREAD        /**< File read stage. */
};

/**
 * @brief Initialise one stage (buffers, operation count, optional calibration).
 *
 * When @p i_compute is non-zero, may run calibration work and broadcast
 * derived @c operations / @c t_op from root. When zero, reuses prior timings
 * and only (re)allocates memory. All ranks must pass the same @p i_compute.
 *
 * @param[in,out] io_stage   Stage to initialise.
 * @param[in]     i_phase    Parent phase (needed for WAIT / I/O pairing).
 * @param[in]     i_group    Current process-group snapshot.
 * @param[in]     i_comm     MPI communicator.
 * @param[in]     i_compute  Non-zero to recalibrate timings.
 * @return Calibration/side-work time accrued (0 for many communication inits).
 *
 * @todo Work should be split across processes.
 * @todo Does not account for heterogeneous machine changes.
 */
double init_stage(stage_t *io_stage, phase_t *i_phase, group_data i_group, MPI_Comm i_comm,
                  int i_compute);

/**
 * @brief Execute one stage of an iteration according to @c stage.pt.
 *
 * @param[in] i_stage Stage descriptor (by value).
 * @param[in] i_group Current process-group snapshot.
 * @param[in] i_comm  MPI communicator.
 * @return Accumulated kernel return / placeholder (not the timing source).
 */
double process_stage(stage_t i_stage, group_data i_group, MPI_Comm i_comm);

#endif
