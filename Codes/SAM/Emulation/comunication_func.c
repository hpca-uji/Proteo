#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include "comunication_func.h"

/**
 * @file comunication_func.c
 * @brief Implementation of SAM synthetic point-to-point patterns.
 */

void point_to_point(int i_myId, int i_numP, int i_root, MPI_Comm i_comm, char *io_array, int i_qty) {
  int prev, next;
  next = (i_myId + 1) % i_numP;
  prev = (i_myId == 0 ? i_numP - 1 : i_myId - 1);

  if (i_myId == i_root) {
    MPI_Send(io_array, i_qty, MPI_CHAR, next, 99, i_comm);
    MPI_Recv(io_array, i_qty, MPI_CHAR, prev, 99, i_comm, MPI_STATUS_IGNORE);
  } else {
    MPI_Recv(io_array, i_qty, MPI_CHAR, prev, 99, i_comm, MPI_STATUS_IGNORE);
    MPI_Send(io_array, i_qty, MPI_CHAR, next, 99, i_comm);
  }
}

void point_to_point_inter(int i_myId, int i_numP, MPI_Comm i_comm, char *i_array, char *o_r_array, int i_qty) {
  int target;
  target = (i_myId + i_numP / 2) % i_numP;
  MPI_Sendrecv(i_array, i_qty, MPI_CHAR, target, 99, o_r_array, i_qty, MPI_CHAR, target, 99, i_comm, MPI_STATUS_IGNORE);
}

void point_to_point_asynch_inter(int i_myId, int i_numP, MPI_Comm i_comm, char *i_array,
                                 char *o_r_array, int i_qty, MPI_Request *o_reqs) {
  int target;
  target = (i_myId + i_numP / 2) % i_numP;
  if (i_myId < i_numP / 2) {
    MPI_Isend(i_array, i_qty, MPI_CHAR, target, 99, i_comm, &(o_reqs[0]));
    MPI_Irecv(o_r_array, i_qty, MPI_CHAR, target, 99, i_comm, &(o_reqs[1]));
  } else {
    MPI_Irecv(o_r_array, i_qty, MPI_CHAR, target, 99, i_comm, &(o_reqs[0]));
    MPI_Isend(i_array, i_qty, MPI_CHAR, target, 99, i_comm, &(o_reqs[1]));
  }
}
