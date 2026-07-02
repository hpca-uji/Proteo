#ifndef PROCESS_PHASE_H
#define PROCESS_PHASE_H

#include "Main_datatypes.h"
#include "results.h"

void init_phases(group_data *group, configuration *config_file, results_data *results, int compute, MPI_Comm comm);

int phase_normal(group_data *group, configuration *config_file, results_data *results, MPI_Comm comm);
int phase_reconf(group_data *group, configuration *config_file, results_data *results, void (*callback)(void *), MPI_Comm comm);

#endif