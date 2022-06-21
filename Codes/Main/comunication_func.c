#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include "comunication_func.h"

/*
 * Realiza una comunicación punto a punto en orden
 * El proceso X envia a X+1 y recibe de X-1
 */
void point_to_point(int myId, int numP, int root, MPI_Comm comm, char *array, int qty) {
  int prev, next;
  next = (myId+1) % numP;
  prev = (myId == 0 ? numP-1 : myId-1);

  if(myId == root) {
    MPI_Send(array, qty, MPI_CHAR, next, 99, comm);
    MPI_Recv(array, qty, MPI_CHAR, prev, 99, comm, MPI_STATUS_IGNORE);
  } else {
    MPI_Recv(array, qty, MPI_CHAR, prev, 99, comm, MPI_STATUS_IGNORE);
    MPI_Send(array, qty, MPI_CHAR, next, 99, comm);
  }
}
