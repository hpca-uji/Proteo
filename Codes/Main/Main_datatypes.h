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
