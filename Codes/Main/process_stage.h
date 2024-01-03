#ifndef PROCESS_STAGE_H
#define PROCESS_STAGE_H

#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include "Main_datatypes.h"

enum compute_methods{COMP_PI, COMP_MATRIX, COMP_POINT, COMP_IPOINT, COMP_WAIT, COMP_BCAST, COMP_ALLGATHER, COMP_REDUCE, COMP_ALLREDUCE};

double init_stage(configuration *config_file, int stage_i, group_data group, MPI_Comm comm, int compute);
//double stage_init_all();
double process_stage(configuration config_file, iter_stage_t stage, group_data group, MPI_Comm comm);
#endif
