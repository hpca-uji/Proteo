#ifndef RESULTS_H
#define RESULTS_H

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

#define RESULTS_INIT_DATA_QTY 100

typedef struct {
  // Iters data
  double *iters_time, **stage_times;
  size_t iters_async, iter_index, iters_size;

  // Spawn, Thread, Sync, Async and Exec time
  double spawn_start, *spawn_time, *spawn_real_time;
  double sync_start, sync_end,  *sync_time;
  double async_start, async_end, *async_time;
  double exec_start, exec_time;
  double wasted_time; // Time spent recalculating iter stages
} results_data;

void comm_results(results_data *results, int root, size_t resizes, MPI_Comm intercomm);

void set_results_post_reconfig(results_data *results, int grp, int sdr, int adr);
void reset_results_index(results_data *results);

void compute_results_iter(results_data *results, int myId, int root, MPI_Comm comm);
void compute_results_stages(results_data *results, int myId, int root, int n_stages, MPI_Comm comm);

void print_iter_results(results_data results);
void print_stage_results(results_data results, size_t n_stages);
void print_global_results(results_data results, size_t resizes);

void init_results_data(results_data *results, size_t resizes, size_t stages, size_t iters_size);
void realloc_results_iters(results_data *results, size_t stages, size_t needed);
void free_results_data(results_data *results, size_t stages);

#endif
