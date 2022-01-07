#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>

typedef struct
{
    int resizes;
    int actual_resize;
    int matrix_tam, comm_tam, sdr, adr;
    int css, cst;
    int aib;
    float general_time;
    double Top;

    int *iters, *procs, *phy_dist;
    float *factors;

} configuration;

configuration *read_ini_file(char *file_name);
void free_config(configuration *user_config);
void print_config(configuration *user_config, int grp);
void print_config_group(configuration *user_config, int grp);

// MPI Intercomm functions
void send_config_file(configuration *config_file, int root, MPI_Comm intercomm);
configuration *recv_config_file(int root, MPI_Comm intercomm);
