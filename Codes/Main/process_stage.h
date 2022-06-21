#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
//#include "Main_datatypes.h"
//#include "../malleability/malleabilityManager.h" //FIXME Refactor

enum compute_methods{COMP_PI, COMP_MATRIX, COMP_POINT, COMP_BCAST, COMP_ALLTOALL, COMP_REDUCE};

//FIXME Refactor el void
void init_stage(void *config_file_void, int stage, void *group_void, MPI_Comm comm, int compute);
double process_stage(void *config_file_void, int stage, void* group_void, MPI_Comm comm);

double latency(int myId, int numP, MPI_Comm comm);
double bandwidth(int myId, int numP, MPI_Comm comm, double latency, int n);
