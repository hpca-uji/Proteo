#ifndef MAIN_DATATYPES_H
#define MAIN_DATATYPES_H

#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include "../MaM/distribution_methods/block_distribution.h"


#define ROOT 0

typedef struct
{
  int pt; // Procedure type to execute
  int id; // Stage identifier
  // Wether the stage completes after "operations" iterations (0)
  // or after "t_stage" time has passed (1).
  int t_capped; 
  double t_stage; // Time to complete the stage

  double t_op;
  int operations, granularity;
  int bytes, real_bytes, my_bytes;
  
  // Arrays to communicate data;
  char* array, *full_array;
  double* double_array;
  int req_count;
  MPI_Request *reqs;

  // Arrays to indicate how many bytes are received from each rank
  struct Counts counts;

} stage_t;

typedef struct
{
  size_t qty_stages;
  size_t qty_iters;
  stage_t *stages;
} phase_t;

typedef struct
{
  int iters, procs;
  int sm, phy_dist, rm;
  int *ss, *rs;
  size_t ss_len, rs_len;
  float factor;
} group_config_t;

typedef struct
{
    size_t n_groups, n_resizes, n_phases; // n_groups==n_resizes+1
    int rigid_times, capture_method;
    size_t sdr, adr;

    MPI_Datatype config_type, group_type, group_strats_type, phase_type, stage_type;
    phase_t *phases;
    group_config_t *groups;
} configuration;

typedef struct {
  int myId;
  int numP;
  unsigned int grp;
  int argc;
  size_t sync_data_groups, async_data_groups;
  size_t start_phase, actual_phase, actual_iter;
  int exec_iters;

  MPI_Comm children, parents;

  char **argv;
  char **sync_array, **async_array;
  int *sync_qty, *async_qty;
  group_config_t grp_config;
} group_data;

#endif
