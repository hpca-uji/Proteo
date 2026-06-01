#ifndef MAM_BLOCK_DISTRIBUTION_H
#define MAM_BLOCK_DISTRIBUTION_H

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

struct Dist_data {
  size_t ini; //Primer elemento a enviar
  size_t fin; //Ultimo elemento a enviar

  size_t tamBl; // Total de elementos
  size_t qty; // Total number of rows of the full disperse matrix

  int myId;
  int numP;
  MPI_Comm intercomm;
};

struct Counts {
  int len, idI, idE;
  MPI_Aint first_target_displs; // RMA. Indicates displacement for first target when performing a Get.
  MPI_Count *counts;
  MPI_Aint *displs;
};

void prepare_comm_alltoall(int myId, int numP, int numP_other, size_t n, int offset_ids, struct Counts *counts);
void prepare_comm_allgatherv(int numP, int n, struct Counts *counts);
void get_block_dist(size_t qty, int id, int numP, struct Dist_data *dist_data);

void mallocCounts(struct Counts *counts, size_t numP);
void freeCounts(struct Counts *counts);
void print_counts(struct Dist_data data_dist, MPI_Count *xcounts, MPI_Aint *xdispls, int size, int include_zero, const char* name);

#endif
