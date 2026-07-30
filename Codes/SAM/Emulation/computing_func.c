#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <mpi.h>
#include "computing_func.h"

/**
 * @file computing_func.c
 * @brief Implementation of SAM synthetic compute kernels.
 */

double computeMatrix(double *i_matrix, int i_n) {
  int row, col;
  double aux;

  aux = 0;
  for (row = 0; row < i_n; row++) {
    for (col = 0; col < i_n; col++) {
      aux += (int)(i_matrix[row * i_n + col] * i_matrix[row * i_n + col]);
    }
  }

  return aux;
}

double computePiSerial(int i_n) {
    int i;
    double h, sum, x, pi;

    h   = 1.0 / (double)i_n;  // Width of each rectangle
    sum = 0.0;
    for (i = 0; i < i_n; i++) {
        x = h * ((double)i + 0.5);   // Midpoint of the rectangle
        sum += 4.0 / (1.0 + x * x);
    }

    return pi = h * sum;
    //MPI_Reduce(&sum, &res, 1, MPI_DOUBLE, MPI_SUM, root, MPI_COMM_WORLD);
}

void initMatrix(double **io_matrix, size_t i_n) {
  size_t i, j;
  double *aux = NULL;

  freeMatrix(io_matrix);

  // Init matrix
  aux = (double *)malloc(i_n * i_n * sizeof(double));
  if (aux == NULL) { perror("Computing matrix could not be allocated"); MPI_Abort(MPI_COMM_WORLD, -1); }

  for (i = 0; i < i_n; i++) {
    for (j = 0; j < i_n; j++) {

      aux[i * i_n + j] = (i + j) * 1.1;
    }
  }
  *io_matrix = aux;
}

void freeMatrix(double **io_matrix) {
  if (*io_matrix != NULL) {
    free(*io_matrix);
    *io_matrix = NULL;
  }
}
