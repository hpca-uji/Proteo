#ifndef MAM_TYPES_H
#define MAM_TYPES_H

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "MAM_Constants.h"

#define MAM_TYPES_INIT_DATA_QTY 100

typedef struct {
  size_t entries; // Indica numero de vectores a comunicar (replicated data)
  size_t max_entries;
  size_t *qty; // Indica numero de elementos en cada subvector de sync_array
  MPI_Datatype *types;

  // Vector de vectores de request. En cada elemento superior se indican los requests a comprobar para dar por finalizada 
  // la comunicacion de ese dato
  size_t *request_qty;
  int *idS; // Used only by RMA for unlocks, depends on entries
  MPI_Request **requests; 
  MPI_Win *windows;
  void **arrays; // Cada subvector es una serie de datos a comunicar

} malleability_data_t;

void add_data(void *data, size_t total_qty, MPI_Datatype type, size_t request_qty, malleability_data_t *data_struct);
void modify_data(void *data, size_t index, size_t total_qty, MPI_Datatype type, size_t request_qty, malleability_data_t *data_struct);
void comm_data_info(malleability_data_t *data_struct_rep, malleability_data_t *data_struct_dist, int is_children_group);
void free_malleability_data_struct(malleability_data_t *data_struct);

#endif
