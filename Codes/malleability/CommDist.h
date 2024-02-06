#ifndef COMMDIST_H
#define COMMDIST_H

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <string.h>
#include "malleabilityStates.h"

void sync_communication(void *send, void **recv, int qty, MPI_Datatype datatype, int numP, int numO, int is_children_group, MPI_Comm comm);

void async_communication_start(void *send, void **recv, int qty, MPI_Datatype datatype, int numP, int numO, int is_children_group, MPI_Comm comm, MPI_Request **requests, size_t *request_qty, MPI_Win *win);
int async_communication_check(int is_children_group, MPI_Request *requests, size_t request_qty);
void async_communication_wait(MPI_Request *requests, size_t request_qty);
void async_communication_end(MPI_Request *requests, size_t request_qty, MPI_Win *win);


void malloc_comm_array(char **array, int qty, int myId, int numP);
#endif
