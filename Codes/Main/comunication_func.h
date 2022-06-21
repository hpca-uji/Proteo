#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>

void point_to_point(int myId, int numP, int root, MPI_Comm comm, char *array, int qty);
