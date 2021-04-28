#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

typedef struct {
  // Iters data
  double *iters_time;
  int *iters_type, iter_index;

  // Spawn, Sync and Async time
  double spawn_start, *spawn_time;
  double sync_start,  *sync_time;
  double async_start, *async_time;
} results_data;


void send_results(results_data *results, int root, MPI_Comm intercomm);
void recv_results(results_data *results, int root, MPI_Comm intercomm);

void print_iter_results(results_data *results, int last_normal_iter_index);
void init_results_data(results_data **results, int resizes, int iters_size);
void free_results_data(results_data **results);
