#ifndef MALLEABILITY_MANAGER_H
#define MALLEABILITY_MANAGER_H

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mpi.h>
#include "malleabilityStates.h"

typedef struct {
  int numS, numT;
  int rank_state;
  MPI_Comm comm;
} mam_user_reconf_t;

int MAM_Init(int root, MPI_Comm *comm, char *name_exec, void (*user_function)(void *), void *user_args);
void MAM_Finalize();
int MAM_Checkpoint(int *mam_state, int wait_completed, void (*user_function)(void *), void *user_args);
void MAM_Resume_redistribution(int *mam_state);


int MAM_Get_Reconf_Info(mam_user_reconf_t *reconf_info);

//void MAM_Data_add(void *data, size_t *index, size_t total_qty, MPI_Datatype type, int is_replicated, int is_constant);
//void MAM_Data_modify(void *data, size_t index, size_t total_qty, MPI_Datatype type, int is_replicated, int is_constant);
//void MAM_Data_get_entries(int is_replicated, int is_constant, size_t *entries);
//void MAM_Data_get_pointer(size_t index, int is_replicated, int is_constant, void **data, size_t *total_qty, size_t *local_qty);

void malleability_add_data(void *data, size_t total_qty, MPI_Datatype type, int is_replicated, int is_constant);
void malleability_modify_data(void *data, size_t index, size_t total_qty, MPI_Datatype type, int is_replicated, int is_constant);
void malleability_get_entries(size_t *entries, int is_replicated, int is_constant);
void malleability_get_data(void **data, size_t index, int is_replicated, int is_constant);

void MAM_Retrieve_times(double *sp_time, double *sy_time, double *asy_time, double *mall_time);

#endif
