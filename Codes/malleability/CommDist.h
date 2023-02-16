#ifndef COMMDIST_H
#define COMMDIST_H

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <string.h>
#include "malleabilityStates.h"

//#define MAL_COMM_COMPLETED 0
//#define MAL_COMM_UNINITIALIZED 2
//#define MAL_ASYNC_PENDING 1

//#define MAL_USE_NORMAL 0
//#define MAL_USE_IBARRIER 1
//#define MAL_USE_POINT 2
//#define MAL_USE_THREAD 3

int sync_communication(char *send, char **recv, int qty, int myId, int numP, int numO, int is_children_group, int comm_type, MPI_Comm comm);

int send_async(char *array, int qty, int myId, int numP, MPI_Comm intercomm, int numP_child, MPI_Request **comm_req, int parents_wait);
void recv_async(char **array, int qty, int myId, int numP, MPI_Comm intercomm, int numP_parents, int parents_wait);


void malloc_comm_array(char **array, int qty, int myId, int numP);
#endif
