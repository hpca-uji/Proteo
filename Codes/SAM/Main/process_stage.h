#ifndef PROCESS_STAGE_H
#define PROCESS_STAGE_H

#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include "Main_datatypes.h"

//                   0        1            2           3            4          5           6               7            8
enum compute_methods{COMP_PI, COMP_MATRIX, COMP_POINT, COMP_IPOINT, COMP_WAIT, COMP_BCAST, COMP_ALLGATHER, COMP_REDUCE, COMP_ALLREDUCE};

double init_stage(stage_t *stage, phase_t *phase, group_data group, MPI_Comm comm, int compute);
//double stage_init_all();
double process_stage(stage_t stage, group_data group, MPI_Comm comm);
#endif
