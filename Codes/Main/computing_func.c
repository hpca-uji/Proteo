#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <mpi.h>
#include "computing_func.h"

/*
 * Realiza una multiplicación de matrices de tamaño n
 */
double computeMatrix(double *matrix, int n) { //FIXME No da tiempos repetibles
  int row, col;
  double aux;

  aux=0;
  for(row=0; row<n; row++) {
    for(col=0; col<n; col++) {
      aux += ( (int)(matrix[row*n + col] + exp(sqrt(row*col))) % n);
    }
  }
  return aux;
}

double computePiSerial(int n) {
    int i;
    double h, sum, x, pi;

    h   = 1.0 / (double) n;  //wide of the rectangle
    sum = 0.0;
    for (i = 0; i < n; i++) {
        x = h * ((double)i + 0.5);   //height of the rectangle
        sum += 4.0 / (1.0 + x*x);
    }

    return pi = h * sum;
    //MPI_Reduce(&sum, &res, 1, MPI_DOUBLE, MPI_SUM, root, MPI_COMM_WORLD);
}



/*
 * Init matrix
 */
void initMatrix(double **matrix, int n) {
  int i, j;

  // Init matrix
  if(matrix != NULL) {
    *matrix = malloc(n * n * sizeof(double));
    if(*matrix == NULL) { MPI_Abort(MPI_COMM_WORLD, -1);}
    for(i=0; i < n; i++) {
      for(j=0; j < n; j++) {
        (*matrix)[i*n + j] = i+j;
      }
    }
  }
}


void freeMatrix(double **matrix) {
  // Init matrix
  if(*matrix != NULL) {
    free(*matrix);
    *matrix = NULL;
  }
}
