#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include "../Main/Main_datatypes.h"

void init_config(char *file_name, configuration **user_config);
void free_config(configuration *user_config);
void print_config(configuration *user_config);
void print_config_group(configuration *user_config, size_t grp);

// MPI Intercomm functions
void send_config_file(configuration *config_file, int root, MPI_Comm intercomm);
void recv_config_file(int root, MPI_Comm intercomm, configuration **config_file_out);

#endif
