#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <mpi.h>
#include "computing_func.h"
#include "comunication_func.h"
#include "Main_datatypes.h"
#include "process_phase.h"
#include "process_stage.h"
#include "MAM.h"

/**
 * @file process_phase.c
 * @brief Implementation of SAM phase iteration drivers.
 */

double iterate(phase_t *i_phase, double *o_time, double *o_time_stages, int i_rigid_times,
               group_data i_group, MPI_Comm i_comm);
double iterate_with_reconf(phase_t *i_phase, int i_state, results_data *io_results, int i_rigid_times,
                           int i_actual_phase, group_data i_group, MPI_Comm i_comm);

double iterate_relaxed(phase_t *i_phase, double *o_time, double *o_times_stages, group_data i_group,
                       MPI_Comm i_comm);
double iterate_rigid(phase_t *i_phase, double *o_time, double *o_times_stages, group_data i_group,
                     MPI_Comm i_comm);

void init_phases(group_data *io_group, configuration *i_config_file, results_data *io_results,
                 int i_compute, MPI_Comm i_comm) {
  size_t i, ii;
  double time = 0;
  phase_t *phase;

  for (i = 0; i < i_config_file->n_phases; i++) {
    phase = i_config_file->phases + i;
    for (ii = 0; ii < phase->qty_stages; ii++) {
      time += init_stage(phase->stages + ii, phase, *io_group, i_comm, i_compute);
    }
  }
  if (!i_compute) { io_results->wasted_time += time; }
}

int phase_normal(group_data *io_group, configuration *i_config_file, results_data *io_results,
                 MPI_Comm i_comm) {
  int start_iter, max_iter, res;
  double *times_aux, **times_stages_aux;
  size_t recorded_iters, recorded_stages, phase_ind, iter, real_iter;
  phase_t *phase;

  max_iter = (io_group->grp + 1) < i_config_file->n_groups ? i_config_file->groups[io_group->grp].iters : -1;
  res = 0;

  // Start arrays for recording times
  recorded_iters = (i_config_file->phases + io_group->actual_phase)->qty_iters;
  recorded_stages = (i_config_file->phases + io_group->actual_phase)->qty_stages;
  for (phase_ind = io_group->actual_phase; phase_ind < i_config_file->n_phases; phase_ind++) {
    if (recorded_iters < (i_config_file->phases + phase_ind)->qty_iters) { recorded_iters = (i_config_file->phases + phase_ind)->qty_iters; }
    if (recorded_stages < (i_config_file->phases + phase_ind)->qty_stages) { recorded_stages = (i_config_file->phases + phase_ind)->qty_stages; }
  }
  times_aux = malloc(recorded_iters * sizeof *times_aux);
  times_stages_aux = malloc(recorded_iters * sizeof *times_aux);
  for (size_t iter_ind = 0; iter_ind < recorded_iters; iter_ind++) {
    times_stages_aux[iter_ind] = malloc(recorded_stages * sizeof *(times_stages_aux[iter_ind]));
  }
  start_iter = io_group->actual_iter;
  real_iter = 0;

  // Start work
  for (; io_group->actual_phase < i_config_file->n_phases; io_group->actual_phase++) {
    phase = i_config_file->phases + io_group->actual_phase;

    for (; io_group->actual_iter < phase->qty_iters; io_group->actual_iter++) {
      real_iter = io_group->actual_iter - start_iter;
      if (io_group->exec_iters == max_iter) {
        capture_m_iterations(io_results, io_group->actual_phase, real_iter, times_aux, times_stages_aux);
        for (iter = 0; iter < recorded_iters; iter++) { free(times_stages_aux[iter]); }
        free(times_aux);
        free(times_stages_aux);
        return res;
      }

      iterate(phase, times_aux + real_iter, times_stages_aux[real_iter], i_config_file->rigid_times, *io_group, i_comm);
      io_group->exec_iters++;
    }

    real_iter = io_group->actual_iter - start_iter;
    capture_m_iterations(io_results, io_group->actual_phase, real_iter, times_aux, times_stages_aux);
    io_group->actual_iter = start_iter = 0;
  }

  for (iter = 0; iter < recorded_iters; iter++) { free(times_stages_aux[iter]); }
  free(times_aux);
  free(times_stages_aux);
  res = 1;
  return res;
}

int phase_reconf(group_data *io_group, configuration *i_config_file, results_data *io_results,
                 void (*i_callback)(void *), MPI_Comm i_comm) {
  int state, res;
  phase_t *phase;

  state = MAM_NOT_STARTED;
  res = 0;

  MAM_Checkpoint(&state, MAM_CHECK_COMPLETION, i_callback, NULL);
  if (MAM_COMPLETED == state) return res;

  for (; io_group->actual_phase < i_config_file->n_phases; io_group->actual_phase++) {
    phase = i_config_file->phases + io_group->actual_phase;
    for (; io_group->actual_iter < phase->qty_iters; ) {

      iterate_with_reconf(phase, state, io_results, i_config_file->rigid_times, io_group->actual_phase, *io_group, i_comm);
      io_group->actual_iter++;
      MAM_Checkpoint(&state, MAM_CHECK_COMPLETION, i_callback, NULL);
      if (MAM_COMPLETED == state) { return res; }
    }
    io_group->actual_iter = 0;
  }

  // Nothing left to emulate: wait until the MaM checkpoint completes
  if (MAM_COMPLETED != state) {
    MAM_Checkpoint(&state, MAM_WAIT_COMPLETION, i_callback, NULL);
  }

  res = 1;
  return res;
}

/////////////////////////////////////////
/////////////////////////////////////////
//ITERATE FUNCTIONS
/////////////////////////////////////////
/////////////////////////////////////////

/**
 * @brief Simulate one application iteration (relaxed or rigid timing).
 *
 * Duration is driven by the configured stages. Timing mode follows
 * @p i_rigid_times (::iterate_rigid vs ::iterate_relaxed).
 *
 * @param[in]  i_phase        Phase whose stages are executed.
 * @param[out] o_time         Total iteration wall time.
 * @param[out] o_time_stages  Per-stage wall times.
 * @param[in]  i_rigid_times  Non-zero to use barrier-based (rigid) timing.
 * @param[in]  i_group        Current process-group snapshot.
 * @param[in]  i_comm         MPI communicator.
 * @return Accumulated stage return value (not used for timing).
 */
double iterate(phase_t *i_phase, double *o_time, double *o_time_stages, int i_rigid_times,
               group_data i_group, MPI_Comm i_comm) {
  double aux = 0;

  if (i_rigid_times) {
    aux = iterate_rigid(i_phase, o_time, o_time_stages, i_group, i_comm);
  } else {
    aux = iterate_relaxed(i_phase, o_time, o_time_stages, i_group, i_comm);
  }

  return aux;
}

/**
 * @brief Simulate one iteration during a background MaM reconfiguration.
 *
 * Unlike ::iterate, records timing into @p io_results after each iteration.
 * Marks the iteration as async when MaM state is @c MAM_PENDING or
 * @c MAM_USER_PENDING.
 *
 * @param[in]     i_phase         Phase whose stages are executed.
 * @param[in]     i_state         Current MaM state.
 * @param[in,out] io_results      Results structure to update.
 * @param[in]     i_rigid_times   Non-zero for rigid timing.
 * @param[in]     i_actual_phase  Phase index for result capture.
 * @param[in]     i_group         Current process-group snapshot.
 * @param[in]     i_comm          MPI communicator.
 * @return Accumulated stage return value (not used for timing).
 */
double iterate_with_reconf(phase_t *i_phase, int i_state, results_data *io_results, int i_rigid_times,
                           int i_actual_phase, group_data i_group, MPI_Comm i_comm) {
  int is_async = 0;
  double time, *times_stages_aux;
  double aux = 0;

  times_stages_aux = malloc(i_phase->qty_stages * sizeof(double));

  if (i_rigid_times) {
    aux = iterate_rigid(i_phase, &time, times_stages_aux, i_group, i_comm);
  } else {
    aux = iterate_relaxed(i_phase, &time, times_stages_aux, i_group, i_comm);
  }

  // Asynchronous data redistribution (or user pending) is in progress
  if (MAM_PENDING == i_state || MAM_USER_PENDING == i_state) { is_async = 1; }
  capture_iteration(io_results, i_actual_phase, is_async, time, times_stages_aux);
  free(times_stages_aux);

  return aux;
}

/**
 * @brief Perform one iteration without barriers between stages.
 *
 * Per-iteration and per-stage times may be imprecise so that the global
 * execution time stays precise.
 *
 * @param[in]  i_phase         Phase whose stages are executed.
 * @param[out] o_time          Total iteration wall time.
 * @param[out] o_times_stages  Per-stage wall times.
 * @param[in]  i_group         Current process-group snapshot.
 * @param[in]  i_comm          MPI communicator.
 * @return Accumulated stage return value (not used for timing).
 */
double iterate_relaxed(phase_t *i_phase, double *o_time, double *o_times_stages, group_data i_group,
                       MPI_Comm i_comm) {
  size_t i;
  double start_time, start_time_stage, aux = 0;
  start_time = MPI_Wtime(); // Imprecise timings

  for (i = 0; i < i_phase->qty_stages; i++) {
    start_time_stage = MPI_Wtime();
    aux += process_stage(i_phase->stages[i], i_group, i_comm);
    o_times_stages[i] = MPI_Wtime() - start_time_stage;
  }

  *o_time = MPI_Wtime() - start_time;
  return aux;
}

/**
 * @brief Perform one iteration with barriers for precise stage timings.
 *
 * Per-iteration and per-stage times are precise; global execution time may
 * become imprecise due to barrier overhead.
 *
 * @param[in]  i_phase         Phase whose stages are executed.
 * @param[out] o_time          Total iteration wall time.
 * @param[out] o_times_stages  Per-stage wall times.
 * @param[in]  i_group         Current process-group snapshot.
 * @param[in]  i_comm          MPI communicator.
 * @return Accumulated stage return value (not used for timing).
 */
double iterate_rigid(phase_t *i_phase, double *o_time, double *o_times_stages, group_data i_group,
                     MPI_Comm i_comm) {
  size_t i;
  double start_time, start_time_stage, aux = 0;

  MPI_Barrier(i_comm);
  start_time = MPI_Wtime();

  for (i = 0; i < i_phase->qty_stages; i++) {
    start_time_stage = MPI_Wtime();
    aux += process_stage(i_phase->stages[i], i_group, i_comm);
    MPI_Barrier(i_comm);
    o_times_stages[i] = MPI_Wtime() - start_time_stage;
  }

  MPI_Barrier(i_comm);
  *o_time = MPI_Wtime() - start_time; // Store timings
  return aux;
}
