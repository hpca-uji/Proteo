#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int resizes;
    int actual_resize;
    int matrix_tam, sdr, adr;
    float general_time;

    int *iters, *procs, *phy_dist;
    float *factors;

} configuration;


configuration *read_ini_file(char *file_name);

void malloc_config_arrays(configuration *user_config, int resizes);
void free_config(configuration *user_config);

void print_config(configuration *user_config);
