#ifndef RESULTS_H
#define RESULTS_H

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

#define RESULTS_INIT_DATA_QTY 100

enum capture_methods{RESULTS_MAX, RESULTS_MEAN, RESULTS_MEDIAN};

typedef struct {
  // Iters data
  double *iters_time, **stage_times;
  size_t iters_async, iter_index, iters_size, qty_stages;
} results_phase;

typedef struct {
  results_phase *phases_times;

  // Spawn, Thread, Sync, Async and Exec time
  double spawn_start, *spawn_time;
  double sync_end,  *sync_time;
  double async_end, *async_time;
  double user_end, *user_time;
  double malleability_end, *malleability_time;
  double exec_start, exec_time;
  double wasted_time; // Time spent recalculating iter stages
} results_data;

void capture_iteration(results_data *results, size_t phase_ind, int is_async, double time, double *times_stages_aux);
void capture_m_iterations(results_data *results, size_t phase_ind, size_t qty_iters, double *times, double **times_stages_aux);

void results_comm(results_data *results, int root, size_t resizes, MPI_Comm intercomm);

void reset_results_index(results_data *results, size_t phase_ind);
void compute_results_iter(results_data *results, int myId, int numP, int root, size_t phases, size_t actual_phase, int capture_method, MPI_Comm comm);

void print_iter_results(results_data results, size_t phase_ind);
void print_stage_results(results_data results, size_t phase_ind);
void print_global_results(results_data results, size_t resizes);

void init_results_data(results_data *results, size_t resizes, size_t phases, size_t *stages, size_t *iters_size);
void realloc_results_iters(results_data *results, size_t phase_ind, size_t needed);
void free_results_data(results_data *results, size_t phases);

#endif
