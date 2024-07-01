#ifndef MAM_BLOCK_DISTRIBUTION_H
#define MAM_BLOCK_DISTRIBUTION_H

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

struct Dist_data {
  int ini; //Primer elemento a enviar
  int fin; //Ultimo elemento a enviar

  int tamBl; // Total de elementos
  int qty; // Total number of rows of the full disperse matrix

  int myId;
  int numP;
  MPI_Comm intercomm;
};

struct Counts {
  int len, idI, idE;
  int first_target_displs; // RMA. Indicates displacement for first target when performing a Get.
  int *counts;
  int *displs;
};

void prepare_comm_alltoall(int myId, int numP, int numP_other, int n, int offset_ids, struct Counts *counts);
void prepare_comm_allgatherv(int numP, int n, struct Counts *counts);
void get_block_dist(int qty, int id, int numP, struct Dist_data *dist_data);

void mallocCounts(struct Counts *counts, size_t numP);
void freeCounts(struct Counts *counts);
void print_counts(struct Dist_data data_dist, int *xcounts, int *xdispls, int size, int include_zero, const char* name);

#endif
