#ifndef PROCESS_STAGE_H
#define PROCESS_STAGE_H

#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include "Main_datatypes.h"

enum compute_methods{COMP_PI, COMP_MATRIX, COMP_POINT, COMP_BCAST, COMP_ALLGATHER, COMP_REDUCE, COMP_ALLREDUCE};

//FIXME Refactor el void
double init_stage(configuration *config_file, int stage_i, group_data group, MPI_Comm comm, int compute);
double process_stage(configuration config_file, iter_stage_t stage, group_data group, MPI_Comm comm);

double latency(int myId, int numP, MPI_Comm comm);
double bandwidth(int myId, int numP, MPI_Comm comm, double latency, int n);
void linear_regression_stage(iter_stage_t *stage, group_data group, MPI_Comm comm);
#endif
