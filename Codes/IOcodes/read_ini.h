#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>

typedef struct
{
    int resizes;
    int actual_resize;
    int matrix_tam, sdr, adr;
    int aib;
    float general_time;

    int *iters, *procs, *phy_dist;
    float *factors;

} configuration;

configuration *read_ini_file(char *file_name);
void free_config(configuration *user_config);
void print_config(configuration *user_config, int numP);

// MPI Intercomm functions
void send_config_file(configuration *config_file, int root, MPI_Comm intercomm);
configuration *recv_config_file(int root, MPI_Comm intercomm);
