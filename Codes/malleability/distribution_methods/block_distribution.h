#ifndef mall_block_distribution
#define mall_block_distribution

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
  int *counts;
  int *displs;
  int *zero_arr;
};

void prepare_comm_alltoall(int myId, int numP, int numP_other, int n, struct Counts *counts);
void prepare_comm_allgatherv(int numP, int n, struct Counts *counts);
void get_block_dist(int qty, int id, int numP, struct Dist_data *dist_data);

void mallocCounts(struct Counts *counts, int numP);
void freeCounts(struct Counts *counts);
void print_counts(struct Dist_data data_dist, int *xcounts, int *xdispls, int size, int include_zero, const char* name);

#endif
