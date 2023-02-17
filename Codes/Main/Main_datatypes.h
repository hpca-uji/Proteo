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
  unsigned int grp;
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
  // Wether the stage completes after "operations" iterations (0)
  // or after "t_stage" time has passed (1).
  int t_capped; 
  double t_stage; // Time to complete the stage

  double t_op;
  int operations;
  int bytes, real_bytes, my_bytes;
  
  // Arrays to communicate data;
  char* array, *full_array;
  double* double_array;
  // Arrays to indicate how many bytes are received from each rank
  struct Counts counts;

} iter_stage_t;

typedef struct
{
  int iters, procs;
  int sm, ss, phy_dist, rm, rs;
  float factor;
} group_config_t;

typedef struct
{
    size_t n_groups, n_resizes, n_stages; // n_groups==n_resizes+1
    size_t actual_group, actual_stage;
    int rigid_times;
    int granularity, sdr, adr;

    MPI_Datatype config_type, group_type, iter_stage_type;
    iter_stage_t *stages;
    group_config_t *groups;
} configuration;

#endif
