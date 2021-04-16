#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "../IOcodes/read_ini.h"

#define ROOT 0

int work(int n);
void iterate(double *matrix, int n);
void computeMatrix(double *matrix, int n);
void initMatrix(double **matrix, int n);

int main(int argc, char *argv[]) {
    int numP, myId;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &numP);
    MPI_Comm_rank(MPI_COMM_WORLD, &myId);    
/*    
    MPI_Comm_get_parent(&comm_parents);
    if(comm_parents != MPI_COMM_NULL ) { // Si son procesos hijos deben recoger la distribucion
      if(myId == ROOT) {
        printf("Nuevo set de procesos de %d\n", numP);
      }
    } else { // Primer set de procesos inicializan valores

    }
*/
    int res = work(10000);

    if(res) { // Ultimo set de procesos comprueba resultados
	    //RESULTADOS
    }

    configuration *config_file = read_ini_file("test.ini");
    print_config(config_file);

    free_config(config_file);



    MPI_Finalize();
    return 0;
}

/*
 * Bucle de computo principal
 */
int work(int n) {
  int iter, MAXITER=5; //FIXME BORRAR MAXITER
  double *matrix;

  initMatrix(&matrix, n);
  for(iter=0; iter<MAXITER; iter++) {
    iterate(matrix, n);
  }
  return 0;
}

/*
 * Simula la ejecucción de una iteración de computo en la aplicación
 */
void iterate(double *matrix, int n) {
  double start_time, actual_time;
  int TIME=10; //FIXME BORRAR

  start_time = actual_time = MPI_Wtime();
  while (actual_time - start_time > TIME) {
    computeMatrix(matrix, n);
    actual_time = MPI_Wtime();
  }
}

/*
 * Realiza una multiplicación de matrices de tamaño n
 */
void computeMatrix(double *matrix, int n) {
  int row, col, i, aux;

  for(row=0; i<n; row++) {
    /* COMPUTE */
    for(col=0; col<n; col++) {
      aux=0;
      for(i=0; i<n; i++) {
        aux += matrix[row*n + i] * matrix[i*n + col];
      }
    }
  }
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
