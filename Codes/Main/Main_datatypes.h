#ifndef MAIN_DATATYPES_H
#define MAIN_DATATYPES_H

#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include "../malleability/distribution_methods/block_distribution.h"


#define ROOT 0

typedef struct {
  int myId;
  int numP;
  int grp;
  int iter_start;
  int argc;

  int numS; // Cantidad de procesos hijos
  MPI_Comm children, parents;

  char *compute_comm_array, *compute_comm_recv;
  char **argv;
  char *sync_array, *async_array;
} group_data;


typedef struct
{
  int pt; // Procedure type
  double t_stage; // Time to complete the stage

  double t_op;
  int operations;
  int bytes, real_bytes, my_bytes;
  
  // Variables to represent linear regresion
  // for collective calls.
  double slope, intercept;

  // Arrays to communicate data;
  char* array, *full_array;
  double* double_array;
  // Arrays to indicate how many bytes are received from each rank
  //int *counts, *displs;
  struct Counts counts;

} iter_stage_t;

typedef struct
{
    int n_resizes, n_stages;
    int actual_resize, actual_stage;
    int granularity, sdr, adr;
    int sm, ss;
    int at;
    double latency_m, bw_m;

    int *iters, *procs, *phy_dist;
    float *factors;

    double t_op_comms;
    iter_stage_t *stages;
} configuration;

#endif
