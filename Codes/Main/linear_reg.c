#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <mpi.h>
#include "Main_datatypes.h"
#include "linear_reg.h"


// Array for linear regression computation
// Cantidades                          10b 100b 1Kb   5Kb  10Kb   50Kb  100Kb   500Kb   1Mb      10Mb      100Mb
double LR_bytes_array[LR_ARRAY_TAM] = {10, 100, 1000, 5000,10000, 50000,100000, 500000, 1000000, 10000000, 100000000};

// Linear regression
// Y = a +bX
// Bytes = a +b(Time)
//
// X is the independent variable, which correlates to the Communication time
// Y is the dependent variable, which correlates to the number of bytes
//

void lr_avg_plus_diff(int tam, double *array, double *avg, double *diffs);

/*
 * Computes and returns the related Y value to a given linear regression
 */
void lr_calc_Y(double slope, double intercept, double x_value, int *y_result) {
  *y_result = (int) ceil(intercept + slope * x_value);
}


/*
 * Computes the slope and intercept for a given array of times
 * so users can calculate the number of bytes for a given time.
 *
 */
void lr_compute(int tam, double *bytes, double *times, double *slope, double *intercept) {
  int i;
  double avgX, avgY;
  double *diffsX, *diffsY;
  double SSxx, SSxy;

  diffsX = malloc(tam * sizeof(double));
  diffsY = malloc(tam * sizeof(double));
  SSxx = SSxy = 0;

  lr_avg_plus_diff(tam, times, &avgX, diffsX);
  lr_avg_plus_diff(tam, bytes, &avgY, diffsY);

  for(i=0; i<tam; i++) {
    SSxx+= diffsX[i]*diffsX[i];
    SSxy+= diffsX[i]*diffsY[i];
  }
  *slope = SSxy / SSxx;
  *intercept = avgY - (*slope * avgX);
  
  free(diffsX);
  free(diffsY);
}

/*
 * Computes the average of an arrray and 
 * the difference of each element in respect to the average.
 *
 * Returns the average and an the difference of each element.
 */
void lr_avg_plus_diff(int tam, double *array, double *avg, double *diffs) {
  int i;
  double sum = 0;
  for(i=0; i<tam; i++) {
    sum+= array[i];
  }
  *avg = sum / tam;

  for(i=0; i<tam; i++) {
    diffs[i]= *avg - array[i];
  }
}


//======================================================||
//======================================================||
//==================TIMES COMPUTATION===================||
//======================================================||
//======================================================||

/*
 * Obtains an array of times to perform a "Broadcast"
 * operation depending on a predifined set of number of bytes.
 */
void lr_times_bcast(int myId, int numP, int root, MPI_Comm comm, int loop_iters, double *times) {
  int i, j, n;
  double start_time;
  char *aux = NULL;

  for(i=0; i<LR_ARRAY_TAM; i++) {
    n = LR_bytes_array[i];
    aux = malloc(n * sizeof(char));

    for(j=0; j<loop_iters; j++) {
      MPI_Barrier(comm);
      start_time = MPI_Wtime();
      MPI_Bcast(aux, n, MPI_CHAR, root, comm);
      times[i*loop_iters+j] = MPI_Wtime() - start_time;
    }

    free(aux);
    aux = NULL;
  }
}


/*
 * Obtains an array of times to perform an "Allgatherv"
 * operation depending on a predifined set of number of bytes.
 */
/*
void lr_times_allgatherv(int myId, int numP, int root, MPI_Comm comm, int loop_iters, double *times) {
  int i, j, n;
  int *counts, *displs;
  double start_time;
  char *aux = NULL;

  counts = calloc(numP,sizeof(int));
  displs = calloc(numP,sizeof(int));

  for(i=0; i<LR_ARRAY_TAM; i++) {
    n = LR_bytes_array[i];
    aux = malloc( * sizeof(char));
    aux_full = malloc(n * sizeof(char));

    for(j=0; j<loop_iters; j++) {
      MPI_Barrier(comm);
      start_time = MPI_Wtime();
      MPI_Allgatherv(aux, stage_data.my_bytes, MPI_CHAR, aux_full, counts, displs, MPI_CHAR, comm);
      times[i*loop_iters+j] = MPI_Wtime() - start_time;
    }
    

    free(aux);
    free(aux_full);
    aux_full = NULL;
    aux = NULL;
  }
  free(counts);
  free(displs);
}
*/
