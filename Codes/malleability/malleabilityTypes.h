#ifndef MALLEABILITY_TYPES_H
#define MALLEABILITY_TYPES_H

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "malleabilityStates.h"

#define MALLEABILITY_INIT_DATA_QTY 100

typedef struct {
  size_t entries; // Indica numero de vectores a comunicar (replicated data)
  size_t max_entries;
  MPI_Request request_ibarrier; // Request para indicar que los padres esperan a que los hijos terminen de recibir
  size_t *qty; // Indica numero de elementos en cada subvector de sync_array
  int *types;

  // Vector de vectores de request. En cada elemento superior se indican los requests a comprobar para dar por finalizada 
  // la comunicacion de ese dato
  MPI_Request **requests; 
  void **arrays; // Cada subvector es una serie de datos a comunicar

} malleability_data_t;

void add_data(void *data, size_t total_qty, int type, size_t request_qty, malleability_data_t *data_struct);
void modify_data(void *data, size_t index, size_t total_qty, int type, size_t request_qty, malleability_data_t *data_struct);
void comm_data_info(malleability_data_t *data_struct_rep, malleability_data_t *data_struct_dist, int is_children_group, int myId, int root, MPI_Comm intercomm);
void free_malleability_data_struct(malleability_data_t *data_struct);

#endif
