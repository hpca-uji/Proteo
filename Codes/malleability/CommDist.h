#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <string.h>


int send_sync(char *array, int qty, int myId, int numP, int root, MPI_Comm intercomm, int numP_child);
void recv_sync(char **array, int qty, int myId, int numP, int root, MPI_Comm intercomm, int numP_parents);


void malloc_comm_array(char **array, int qty, int myId, int numP);
