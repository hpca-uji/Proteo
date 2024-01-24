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
void MAM_Set_configuration(int spawn_method, int spawn_strategies, int spawn_dist, int red_method, int red_strategies);
void MAM_Set_key_configuration(char *key, int required, int *provided);
void MAM_Set_target_number(int numC); // TODO TO BE DEPRECATED

void malleability_add_data(void *data, size_t total_qty, MPI_Datatype type, int is_replicated, int is_constant);
void malleability_modify_data(void *data, size_t index, size_t total_qty, MPI_Datatype type, int is_replicated, int is_constant);
void malleability_get_entries(size_t *entries, int is_replicated, int is_constant);
void malleability_get_data(void **data, size_t index, int is_replicated, int is_constant);

void MAM_Retrieve_times(double *sp_time, double *sy_time, double *asy_time, double *mall_time);

#endif
