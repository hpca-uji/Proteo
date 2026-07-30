#ifndef RESULTS_H
#define RESULTS_H

/**
 * @file results.h
 * @brief Capture, aggregate, print, and free SAM timing results.
 */

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

/** @brief Default initial capacity hint for iteration result buffers. */
#define RESULTS_INIT_DATA_QTY 100

/**
 * @brief How per-iteration times are reduced across MPI ranks.
 *
 * Values match JSON/INI @c Capture_Method: 0 = max, 1 = mean, 2 = median.
 */
enum capture_methods {
  RESULTS_MAX,    /**< Per-iter maximum across ranks (wall-clock style). */
  RESULTS_MEAN,   /**< Per-iter arithmetic mean across ranks. */
  RESULTS_MEDIAN  /**< Per-iter median across ranks. */
};

/**
 * @brief Per-phase iteration and stage timing buffers.
 */
typedef struct {
  double *iters_time;   /**< Wall time of each recorded iteration. */
  double **stage_times; /**< Per-stage times; [stage][iter]. */
  size_t iters_async;   /**< Count of iters recorded during async MaM activity. */
  size_t iter_index;    /**< Next free slot / number of valid iters. */
  size_t iters_size;    /**< Allocated capacity of the time vectors. */
  size_t qty_stages;    /**< Number of stages in this phase. */
} results_phase;

/**
 * @brief Aggregated timing results for a Proteo run.
 *
 * Per-resize arrays (@c spawn_time, @c sync_time, …) are typically filled
 * via @c MAM_Retrieve_times after each malleability operation.
 */
typedef struct {
  results_phase *phases_times; /**< One ::results_phase per application phase. */

  double *spawn_time;          /**< Process-creation time per resize. */
  double *sync_time;           /**< Synchronous redistribution time per resize. */
  double *async_time;          /**< Asynchronous redistribution time per resize. */
  double *user_time;           /**< User redistribution callback time per resize. */
  double *malleability_time;   /**< Total malleability time per resize. */
  double exec_start;           /**< @c MPI_Wtime at start of useful execution. */
  double exec_time;            /**< Total execution time (minus wasted). */
  double wasted_time;          /**< Time spent recalculating iter stages (excluded from total). */
} results_data;

/**
 * @brief Store monitored times for one iteration of a phase.
 *
 * @param[in,out] io_results         Results structure to update.
 * @param[in]     i_phase_ind        Phase index.
 * @param[in]     i_is_async         Non-zero if the iter ran during async MaM work.
 * @param[in]     i_time             Total iteration time.
 * @param[in]     i_times_stages_aux Per-stage times (length = phase stage count).
 */
void capture_iteration(results_data *io_results, size_t i_phase_ind, int i_is_async,
                       double i_time, double *i_times_stages_aux);

/**
 * @brief Store monitored times for a batch of iterations.
 *
 * Intended for sets of iterations known not to have run during an
 * asynchronous MaM operation; every iter is recorded as non-async.
 *
 * @param[in,out] io_results         Results structure to update.
 * @param[in]     i_phase_ind        Phase index.
 * @param[in]     i_qty_iters        Number of iterations in the batch.
 * @param[in]     i_times            Per-iteration total times.
 * @param[in]     i_times_stages_aux Per-iteration per-stage times.
 */
void capture_m_iterations(results_data *io_results, size_t i_phase_ind, size_t i_qty_iters,
                          double *i_times, double **i_times_stages_aux);

/**
 * @brief Broadcast scalar and per-resize timing fields over an intracommunicator.
 *
 * @param[in,out] io_results   Results structure (filled on non-root ranks).
 * @param[in]     i_root       Broadcast root rank.
 * @param[in]     i_resizes    Number of resize slots in the timing arrays.
 * @param[in]     i_comm       Intracommunicator used for the broadcast.
 */
void results_comm(results_data *io_results, int i_root, size_t i_resizes, MPI_Comm i_comm);

/**
 * @brief Reset the write index for a phase's iteration vectors to zero.
 *
 * Previous iteration values become invalid for external readers. Required
 * after a reconfiguration that uses the MERGE spawn method.
 *
 * @param[in,out] io_results  Results structure.
 * @param[in]     i_phase_ind Phase whose index is reset.
 */
void reset_results_index(results_data *io_results, size_t i_phase_ind);

/**
 * @brief Compute reduction of per-iteration times across ranks for phases from @p i_actual_phase onward.
 *
 * @param[in,out] io_results       Results structure.
 * @param[in]     i_myId           Local MPI rank.
 * @param[in]     i_numP           Communicator size.
 * @param[in]     i_root           Reduction root.
 * @param[in]     i_phases         Total number of phases.
 * @param[in]     i_actual_phase   First phase index to process.
 * @param[in]     i_capture_method One of ::capture_methods.
 * @param[in]     i_comm           MPI communicator.
 */
void compute_results_iter(results_data *io_results, int i_myId, int i_numP, int i_root,
                          size_t i_phases, size_t i_actual_phase, int i_capture_method,
                          MPI_Comm i_comm);

/**
 * @brief Print local iteration results for one phase.
 * @param[in] i_results   Results structure (by value).
 * @param[in] i_phase_ind Phase index.
 */
void print_iter_results(results_data i_results, size_t i_phase_ind);

/**
 * @brief Print local per-stage timing results for one phase.
 * @param[in] i_results   Results structure (by value).
 * @param[in] i_phase_ind Phase index.
 */
void print_stage_results(results_data i_results, size_t i_phase_ind);

/**
 * @brief Print global malleability and execution timing summaries.
 * @param[in] i_results Results structure (by value).
 * @param[in] i_resizes Number of resize entries to print.
 */
void print_global_results(results_data i_results, size_t i_resizes);

/**
 * @brief Allocate and initialise a results structure.
 *
 * @param[in,out] io_results   Results structure to initialise.
 * @param[in]     i_resizes    Number of resize slots for timing arrays.
 * @param[in]     i_phases     Number of phases.
 * @param[in]     i_stages     Per-phase stage counts (length @p i_phases).
 * @param[in]     i_iters_size Per-phase initial iteration capacities.
 */
void init_results_data(results_data *io_results, size_t i_resizes, size_t i_phases,
                       size_t *i_stages, size_t *i_iters_size);

/**
 * @brief Grow iteration/stage time buffers for a phase to at least @p i_needed slots.
 *
 * @param[in,out] io_results  Results structure.
 * @param[in]     i_phase_ind Phase index.
 * @param[in]     i_needed    Required capacity.
 */
void realloc_results_iters(results_data *io_results, size_t i_phase_ind, size_t i_needed);

/**
 * @brief Free all heap memory owned by a results structure.
 * @param[in,out] io_results Results structure.
 * @param[in]     i_phases   Number of phases (size of @c phases_times).
 */
void free_results_data(results_data *io_results, size_t i_phases);

#endif
