#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <mpi.h>
#include "computing_func.h"

/*
 * Realiza una multiplicación de matrices de tamaño n
 */
double computeMatrix(double *matrix, int n) { 
  int row, col;
  double aux;

  aux=0;
  for(row=0; row<n; row++) {
    for(col=0; col<n; col++) {
      aux += (int)(matrix[row*n + col] * matrix[row*n + col]);
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
void initMatrix(double **matrix, size_t n) {
  size_t i, j;
  double *aux = NULL;

  freeMatrix(matrix);

  // Init matrix
  aux = (double *) malloc(n * n * sizeof(double));
  if(aux == NULL) { perror("Computing matrix could not be allocated"); MPI_Abort(MPI_COMM_WORLD, -1);}

  for(i=0; i < n; i++) {
    for(j=0; j < n; j++) {

      aux[i*n + j] = (i+j) * 1.1;
    }
  }
  *matrix = aux;
}



void freeMatrix(double **matrix) {
  // Init matrix
  if(*matrix != NULL) {
    free(*matrix);
    *matrix = NULL;
  }
}
