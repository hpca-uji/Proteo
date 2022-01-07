#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

#define RESULTS_INIT_DATA_QTY 100

typedef struct {
  // Iters data
  double *iters_time;
  int *iters_type, iter_index, iters_size;

  // Spawn, Thread, Sync, Async and Exec time
  double spawn_start, *spawn_time, *spawn_thread_time;
  double sync_start, sync_end,  *sync_time;
  double async_start, async_end, *async_time;
  double exec_start, exec_time;
} results_data;


void send_results(results_data *results, int root, int resizes, MPI_Comm intercomm);
void recv_results(results_data *results, int root, int resizes, MPI_Comm intercomm);

void set_results_post_reconfig(results_data *results, int grp, int sdr, int adr);
void reset_results_index(results_data *results);

void print_iter_results(results_data results, int last_normal_iter_index);
void print_global_results(results_data results, int resizes);
void init_results_data(results_data *results, int resizes, int iters_size);
void realloc_results_iters(results_data *results, int needed);
void free_results_data(results_data *results);
