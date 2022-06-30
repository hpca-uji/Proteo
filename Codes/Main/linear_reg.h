#ifndef LINEAR_REG_H
#define LINEAR_REG_H

#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>

/*----------LINEAR REGRESSION TYPES--------------*/
#define LR_ARRAY_TAM 11
// Array for linear regression computation
extern double LR_bytes_array[LR_ARRAY_TAM];

void lr_calc_Y(double slope, double intercept, double x_value, int *y_result);
void lr_compute(int loop_iters, double *bytes, double *times, double *slope, double *intercept);

void lr_times_bcast(int myId, int numP, int root, MPI_Comm comm, int loop_iters, double *times);

#endif
