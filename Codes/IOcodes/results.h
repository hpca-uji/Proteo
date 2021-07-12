#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

typedef struct {
  // Iters data
  double *iters_time;
  int *iters_type, iter_index, iters_size;

  // Spawn, Sync and Async time
  double spawn_start, *spawn_time;
  double sync_start,  *sync_time;
  double async_start, *async_time;
  double exec_start, exec_time;
} results_data;


void send_results(results_data *results, int root, int resizes, MPI_Comm intercomm);
void recv_results(results_data *results, int root, int resizes, MPI_Comm intercomm);

void print_iter_results(results_data *results, int last_normal_iter_index);
void print_global_results(results_data *results, int resizes);
void init_results_data(results_data **results, int resizes, int iters_size);
void realloc_results_iters(results_data *results, int needed);
void free_results_data(results_data **results);
