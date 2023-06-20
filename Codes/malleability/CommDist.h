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
//int async_communication(char *send, char **recv, int qty, int myId, int numP, int numO, int is_children_group, int red_method, int red_strategies, MPI_Comm comm, MPI_Request **requests, size_t *request_qty);

int async_communication_start(char *send, char **recv, int qty, int myId, int numP, int numO, int is_children_group, int red_method, int red_strategies, MPI_Comm comm, MPI_Request **requests, size_t *request_qty, MPI_Win *win);
int async_communication_check(int myId, int is_children_group, int red_strategies, MPI_Comm comm, MPI_Request *requests, size_t request_qty);
void async_communication_wait(int red_strategies, MPI_Comm comm, MPI_Request *requests, size_t request_qty);
void async_communication_end(int red_method, int red_strategies, MPI_Request *requests, size_t request_qty, MPI_Win *win);
//int send_async(char *array, int qty, int myId, int numP, MPI_Comm intercomm, int numP_child, MPI_Request **comm_req, int red_method, int red_strategies);
//void recv_async(char **array, int qty, int myId, int numP, MPI_Comm intercomm, int numP_parents, int red_method, int red_strategies);


void malloc_comm_array(char **array, int qty, int myId, int numP);
int malleability_red_contains_strat(int comm_strategies, int strategy, int *result);
int malleability_red_add_strat(int *comm_strategies, int strategy);
#endif
