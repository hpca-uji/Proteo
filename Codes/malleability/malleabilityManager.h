#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mpi.h>
#include "../IOcodes/read_ini.h"
#include "../IOcodes/results.h"
#include "malleabilityStates.h"

int init_malleability(int myId, int numP, int root, MPI_Comm comm, char *name_exec);
void free_malleability();
int malleability_checkpoint();
void set_benchmark_grp(int grp);

void set_malleability_configuration(int spawn_type, int spawn_is_single, int spawn_dist, int spawn_threaded, int comm_type, int comm_threaded);
void set_children_number(int numC); // TODO TO BE DEPRECATED
void get_malleability_user_comm(MPI_Comm *comm);

void malleability_add_data(void *data, int total_qty, int type, int is_replicated, int is_constant);
void malleability_get_entries(int *entries, int is_replicated, int is_constant);
void malleability_get_data(void **data, int index, int is_replicated, int is_constant);

void set_benchmark_configuration(configuration *config_file);
void get_benchmark_configuration(configuration **config_file);
void set_benchmark_results(results_data *results);
void get_benchmark_results(results_data **results);
