#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include "Main_datatypes.h"
#include "linear_reg.h"

// Linear regression
// Y = a +bX
// Bytes = a +b(Time)
//
// X is the independent variable, which correlates to the Communication time
// Y is the dependent variable, which correlates to the number of bytes
//

void lr_avg_plus_diff(double *array, double *avg, double *diffs);

/*
 * Computes the slope and intercept for a given array of times
 * so users can calculate the number of bytes for a given time.
 *
 */
void lr_compute(double *times, double *slope, double *intercept) {
  int i;
  double avgX, avgY;
  double *diffsX, *diffsY;
  double SSxx, SSxy;

  diffsX = malloc(LR_ARRAY_TAM * sizeof(double));
  diffsY = malloc(LR_ARRAY_TAM * sizeof(double));
  SSxx = SSxy = 0;

  lr_avg_plus_diff(times, &avgX, diffsX);
  lr_avg_plus_diff(LR_bytes_array, &avgY, diffsY);

  for(i=0; i<LR_ARRAY_TAM; i++) {
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
void lr_avg_plus_diff(double *array, double *avg, double *diffs) {
  int i;
  double sum = 0;
  for(i=0; i<LR_ARRAY_TAM; i++) {
    sum+= array[i];
  }
  *avg = sum / LR_ARRAY_TAM;

  for(i=0; i<LR_ARRAY_TAM; i++) {
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
void lr_times_bcast(int myId, int numP, int root, MPI_Comm comm, double *times) {
  int i, j, n, loop_count = 10;
  double start_time, stop_time, elapsed_time;
  char *aux;
  elapsed_time = 0;

  for(i=0; i<LR_ARRAY_TAM; i++) {
    n = LR_bytes_array[i];
    aux = malloc(n * sizeof(char));

    MPI_Barrier(comm);
    start_time = MPI_Wtime();
    for(j=0; j<loop_count; j++) {
      MPI_Bcast(aux, n, MPI_CHAR, root, comm);
    }
    MPI_Barrier(comm);
    stop_time = MPI_Wtime();

    elapsed_time = (stop_time - start_time) / loop_count;
    MPI_Reduce(MPI_IN_PLACE, &elapsed_time, n, MPI_DOUBLE, MPI_MAX, root, comm);
    if(myId == root) {
      times[i] = elapsed_time;
    }
    free(aux);
  }
}
