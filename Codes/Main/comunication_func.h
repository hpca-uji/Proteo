#ifndef COMUNICATION_FUNC_H
#define COMUNICATION_FUNC_H

#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>


void point_to_point(int myId, int numP, int root, MPI_Comm comm, char *array, int qty);
void point_to_point_inter(int myId, int numP, MPI_Comm comm, char *array, int qty);

#endif
