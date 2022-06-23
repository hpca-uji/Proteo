#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>


#define ROOT 0

typedef struct {
  int myId;
  int numP;
  int grp;
  int iter_start;
  int argc;

  int numS; // Cantidad de procesos hijos
  MPI_Comm children, parents;

  char *compute_comm_array, *compute_comm_recv;
  char **argv;
  char *sync_array, *async_array;
} group_data;


/*----------LINEAR REGRESSION TYPES--------------*/
#define LR_ARRAY_TAM 7
// Array for linear regression computation
// Cantidades               10b 100b 1Kb   100Kb   1Mb      10Mb      100Mb
double LR_bytes_array[LR_ARRAY_TAM] = {10, 100, 1000, 100000, 1000000, 10000000, 100000000};
