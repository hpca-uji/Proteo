#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>

typedef struct
{
  int pt; // Procedure type
  float t_stage; // Time to complete the stage

  double t_op;
  int operations;
  int bytes, bytes_real;

  char* array, *full_array;
  double* double_array;
} iter_stage_t;

typedef struct
{
    int resizes, iter_stages;
    int actual_resize, actual_iter;
    int matrix_tam, comm_tam, sdr, adr;
    int css, cst;
    int aib;
    double latency_m, bw_m;

    int *iters, *procs, *phy_dist;
    float *factors;

    iter_stage_t *iter_stage;
} configuration;


configuration *read_ini_file(char *file_name);
void free_config(configuration *user_config);
void print_config(configuration *user_config, int grp);
void print_config_group(configuration *user_config, int grp);

// MPI Intercomm functions
void send_config_file(configuration *config_file, int root, MPI_Comm intercomm);
void recv_config_file(int root, MPI_Comm intercomm, configuration **config_file_out);
