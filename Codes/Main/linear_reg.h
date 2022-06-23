#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>


void lr_compute(double *times, double *slope, double *intercept);

void lr_times_bcast(int myId, int numP, int root, MPI_Comm comm, double *times);
