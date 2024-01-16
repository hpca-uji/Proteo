#ifndef MALLEABILITY_MANAGER_H
#define MALLEABILITY_MANAGER_H

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mpi.h>
#include "../Main/configuration.h"
#include "../Main/Main_datatypes.h"
#include "malleabilityStates.h"

int init_malleability(int myId, int numP, int root, MPI_Comm comm, char *name_exec, char *nodelist, int num_cpus, int num_nodes);
void free_malleability();
void indicate_ending_malleability(int new_outside_state);
int malleability_checkpoint(int *mam_state, int wait_completed);
void MAM_Commit(int *mam_state, MPI_Comm *new_comm);

void set_malleability_configuration(int spawn_method, int spawn_strategies, int spawn_dist, int red_method, int red_strategies);
void set_children_number(int numC); // TODO TO BE DEPRECATED
void get_malleability_user_comm(MPI_Comm *comm);

void malleability_add_data(void *data, size_t total_qty, MPI_Datatype type, int is_replicated, int is_constant);
void malleability_modify_data(void *data, size_t index, size_t total_qty, MPI_Datatype type, int is_replicated, int is_constant);
void malleability_get_entries(size_t *entries, int is_replicated, int is_constant);
void malleability_get_data(void **data, size_t index, int is_replicated, int is_constant);

void set_benchmark_configuration(configuration *config_file);
void get_benchmark_configuration(configuration **config_file);
void malleability_retrieve_times(double *sp_time, double *sy_time, double *asy_time, double *mall_time);

#endif
